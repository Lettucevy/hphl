import * as path from 'path';
import * as fs from 'fs';
import * as vscode from 'vscode';
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

function findCompilerPath(): string {
  const config = vscode.workspace.getConfiguration('hphl');
  const configured = config.get<string>('compilerPath');
  if (configured && configured.trim().length > 0 && fs.existsSync(configured.trim())) {
    return configured.trim();
  }

  const workspaceFolders = vscode.workspace.workspaceFolders;
  const isWindows = process.platform === 'win32';
  const exeName = isWindows ? 'hphlc.exe' : 'hphlc';

  if (workspaceFolders) {
    for (const folder of workspaceFolders) {
      const candidates = [
        path.join(folder.uri.fsPath, 'bin', exeName),
        path.join(folder.uri.fsPath, 'compiler', 'bin', exeName),
        path.join(folder.uri.fsPath, exeName)
      ];
      for (const cand of candidates) {
        if (fs.existsSync(cand)) {
          return cand;
        }
      }
    }
  }

  // Fallback to searching PATH or simple command
  return exeName;
}

export function activate(context: vscode.ExtensionContext) {
  const outputChannel = vscode.window.createOutputChannel('HPHL');
  outputChannel.appendLine(vscode.l10n.t('[HPHL] Extension activated successfully.'));

  const compilerPath = findCompilerPath();
  outputChannel.appendLine(vscode.l10n.t('[HPHL] Compiler detected: {0}', compilerPath));

  // 1. Iniciar LSP Client
  startLspClient(compilerPath, outputChannel, context);

  // 2. Registrar DAP Debug Adapter Factory
  const debugFactory = new HphlDebugAdapterDescriptorFactory(compilerPath);
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory('hphl', debugFactory)
  );

  const configProvider = new HphlDebugConfigurationProvider();
  context.subscriptions.push(
    vscode.debug.registerDebugConfigurationProvider('hphl', configProvider)
  );

  // 3. Status Bar Item para exibir e alternar o backend ativo (x64 / llvm / ir)
  const statusBarBackend = vscode.window.createStatusBarItem(
    vscode.StatusBarAlignment.Right,
    100
  );
  statusBarBackend.command = 'hphl.selectBackend';
  statusBarBackend.tooltip = vscode.l10n.t('Click to switch HPHL compiler backend (x64 / llvm / ir)');
  context.subscriptions.push(statusBarBackend);

  function getActiveBackend(): string {
    return vscode.workspace.getConfiguration('hphl').get<string>('backend') || 'x64';
  }

  function updateStatusBar() {
    const editor = vscode.window.activeTextEditor;
    if (editor && editor.document.languageId === 'hphl') {
      const b = getActiveBackend();
      statusBarBackend.text = `$(gear) HPHL: ${b}`;
      statusBarBackend.show();
    } else {
      statusBarBackend.hide();
    }
  }

  context.subscriptions.push(
    vscode.window.onDidChangeActiveTextEditor(() => updateStatusBar()),
    vscode.workspace.onDidChangeConfiguration(e => {
      if (e.affectsConfiguration('hphl.backend')) {
        updateStatusBar();
      }
    })
  );
  updateStatusBar();

  // 4. Registrar Comandos
  context.subscriptions.push(
    vscode.commands.registerCommand('hphl.selectBackend', async () => {
      const current = getActiveBackend();
      const items: vscode.QuickPickItem[] = [
        {
          label: 'x64',
          description: vscode.l10n.t('Fast native x86_64 (instant compilation, direct emit)'),
          picked: current === 'x64'
        },
        {
          label: 'llvm',
          description: vscode.l10n.t('LLVM Optimizer Backend (SIMD auto-vectorization, -O2 optimization pipeline)'),
          picked: current === 'llvm'
        },
        {
          label: 'ir',
          description: vscode.l10n.t('Generate and dump textual LLVM IR (.ll)'),
          picked: current === 'ir'
        }
      ];

      const selected = await vscode.window.showQuickPick(items, {
        placeHolder: vscode.l10n.t('Select HPHL compiler backend (current: {0})', current)
      });

      if (selected) {
        await vscode.workspace.getConfiguration('hphl').update('backend', selected.label, vscode.ConfigurationTarget.Global);
        updateStatusBar();
        vscode.window.showInformationMessage(vscode.l10n.t('HPHL backend changed to: {0}', selected.label));
      }
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('hphl.debugFile', async () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor) {
        vscode.window.showErrorMessage(vscode.l10n.t('No active HPHL file.'));
        return;
      }
      await editor.document.save();
      await vscode.debug.startDebugging(undefined, {
        type: 'hphl',
        name: vscode.l10n.t('HPHL: Debug Current File'),
        request: 'launch',
        program: editor.document.fileName,
        cwd: vscode.workspace.workspaceFolders?.[0]?.uri.fsPath || path.dirname(editor.document.fileName),
        stopOnEntry: false
      });
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('hphl.restartLsp', async () => {
      outputChannel.appendLine(vscode.l10n.t('Restarting LSP server...'));
      if (client) {
        await client.stop();
        client = undefined;
      }
      const newPath = findCompilerPath();
      startLspClient(newPath, outputChannel, context);
      vscode.window.showInformationMessage(vscode.l10n.t('HPHL LSP server restarted.'));
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('hphl.runFile', async () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor) {
        vscode.window.showErrorMessage(vscode.l10n.t('No active HPHL file.'));
        return;
      }
      const filePath = editor.document.fileName;
      if (!filePath.endsWith('.hphl')) {
        vscode.window.showWarningMessage(vscode.l10n.t('Active file is not a .hphl file.'));
        return;
      }

      await editor.document.save();
      const terminal = vscode.window.createTerminal('HPHL Run');
      terminal.show();

      const comp = findCompilerPath();
      const backend = getActiveBackend();
      const isWin = process.platform === 'win32';
      const outExe = isWin ? filePath.replace(/\.hphl$/, '.exe') : filePath.replace(/\.hphl$/, '');

      if (isWin) {
        const baseName = path.basename(outExe, '.exe');
        terminal.sendText(`Stop-Process -Name "${baseName}" -Force -ErrorAction SilentlyContinue; & "${comp}" "${filePath}" --backend ${backend} -o "${outExe}"; if ($?) { & "${outExe}" }`);
      } else {
        terminal.sendText(`"${comp}" "${filePath}" --backend ${backend} -o "${outExe}" && "${outExe}"`);
      }
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('hphl.buildFile', async () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor) {
        vscode.window.showErrorMessage(vscode.l10n.t('No active HPHL file.'));
        return;
      }
      const filePath = editor.document.fileName;
      await editor.document.save();

      const comp = findCompilerPath();
      const backend = getActiveBackend();
      const terminal = vscode.window.createTerminal('HPHL Build');
      terminal.show();
      const isWin = process.platform === 'win32';
      if (isWin) {
        const baseName = path.basename(filePath, '.hphl');
        terminal.sendText(`Stop-Process -Name "${baseName}" -Force -ErrorAction SilentlyContinue; & "${comp}" "${filePath}" --backend ${backend}`);
      } else {
        terminal.sendText(`"${comp}" "${filePath}" --backend ${backend}`);
      }
    })
  );
}

function startLspClient(
  compilerPath: string,
  outputChannel: vscode.OutputChannel,
  context: vscode.ExtensionContext
) {
  const serverOptions: ServerOptions = {
    command: compilerPath,
    args: ['--lsp'],
    transport: TransportKind.stdio
  };

  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: 'file', language: 'hphl' }],
    outputChannel: outputChannel,
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher('**/*.hphl')
    }
  };

  client = new LanguageClient('hphlLsp', 'HPHL Language Server', serverOptions, clientOptions);
  client.start().catch(err => {
    outputChannel.appendLine(`[HPHL] Error starting LSP: ${err}`);
  });
  context.subscriptions.push({
    dispose: () => {
      if (client) {
        return client.stop();
      }
    }
  });
}

class HphlDebugAdapterDescriptorFactory implements vscode.DebugAdapterDescriptorFactory {
  constructor(private defaultCompilerPath: string) {}

  createDebugAdapterDescriptor(
    session: vscode.DebugSession
  ): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
    const customCompiler = session.configuration.compilerPath || findCompilerPath();
    return new vscode.DebugAdapterExecutable(customCompiler, ['--dap']);
  }
}

class HphlDebugConfigurationProvider implements vscode.DebugConfigurationProvider {
  resolveDebugConfiguration(
    folder: vscode.WorkspaceFolder | undefined,
    config: vscode.DebugConfiguration,
    _token?: vscode.CancellationToken
  ): vscode.ProviderResult<vscode.DebugConfiguration> {
    if (!config.type && !config.request && !config.name) {
      const editor = vscode.window.activeTextEditor;
      if (editor && editor.document.languageId === 'hphl') {
        config.type = 'hphl';
        config.name = vscode.l10n.t('HPHL: Start Debugging');
        config.request = 'launch';
        config.program = '${file}';
        config.cwd = '${workspaceFolder}';
        config.stopOnEntry = false;
      }
    }

    if (!config.program) {
      vscode.window.showErrorMessage(vscode.l10n.t('No .hphl file specified for debugging.'));
      return undefined;
    }

    return config;
  }
}

export function deactivate(): Thenable<void> | undefined {
  if (!client) {
    return undefined;
  }
  return client.stop();
}
