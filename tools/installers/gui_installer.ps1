# ==============================================================================
# HP-HL SDK — Instalador Gráfico Oficial (Windows WPF Dark Theme)
# ==============================================================================
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase, System.Windows.Forms

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
# Se executado dentro do SDK, o RootDir é a pasta atual; se executado em tools/installers, é o Root do repo
$SdkRootDir = if (Test-Path (Join-Path $ScriptDir "bin/hphlc.exe")) { $ScriptDir } else { (Resolve-Path (Join-Path $ScriptDir "../..")).Path }

# Fallback para caminho padrão de instalação
$DefaultInstallPath = Join-Path $env:USERPROFILE ".hphl"

[xml]$xaml = @"
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Instalador HP-HL SDK v1.0.0" 
        Height="580" Width="680" 
        WindowStartupLocation="CenterScreen" 
        ResizeMode="NoResize"
        Background="#0b0f19" 
        Foreground="#f8fafc"
        FontFamily="Segoe UI">
    <Window.Resources>
        <Style TargetType="CheckBox">
            <Setter Property="Foreground" Value="#e2e8f0"/>
            <Setter Property="FontSize" Value="13"/>
            <Setter Property="Margin" Value="0,4,0,4"/>
            <Setter Property="Cursor" Value="Hand"/>
        </Style>
        <Style TargetType="Button">
            <Setter Property="Background" Value="#1e293b"/>
            <Setter Property="Foreground" Value="#f8fafc"/>
            <Setter Property="FontSize" Value="13"/>
            <Setter Property="FontWeight" Value="SemiBold"/>
            <Setter Property="BorderBrush" Value="#334155"/>
            <Setter Property="BorderThickness" Value="1"/>
            <Setter Property="Padding" Value="14,7"/>
            <Setter Property="Cursor" Value="Hand"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border Background="{TemplateBinding Background}" 
                                BorderBrush="{TemplateBinding BorderBrush}" 
                                BorderThickness="{TemplateBinding BorderThickness}" 
                                CornerRadius="6">
                            <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
                        </Border>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
    </Window.Resources>

    <Grid Margin="24">
        <Grid.RowDefinitions>
            <RowDefinition Height="Auto"/> <!-- Header -->
            <RowDefinition Height="Auto"/> <!-- Path Selection -->
            <RowDefinition Height="*"/>    <!-- Options List -->
            <RowDefinition Height="Auto"/> <!-- Progress -->
            <RowDefinition Height="Auto"/> <!-- Buttons -->
        </Grid.RowDefinitions>

        <!-- HEADER -->
        <Border Grid.Row="0" Background="#111827" CornerRadius="8" Padding="16" Margin="0,0,0,16" BorderBrush="#1f2937" BorderThickness="1">
            <Grid>
                <Grid.ColumnDefinitions>
                    <ColumnDefinition Width="*"/>
                    <ColumnDefinition Width="Auto"/>
                </Grid.ColumnDefinitions>
                <StackPanel Grid.Column="0">
                    <TextBlock Text="HP-HL SDK v1.0.0" FontSize="20" FontWeight="Bold" Foreground="#38bdf8"/>
                    <TextBlock Text="High-Performance Systems Language Setup" FontSize="12" Foreground="#94a3b8" Margin="0,2,0,0"/>
                    <TextBlock Text="Selecione o destino e os componentes opcionais para instalar." FontSize="11" Foreground="#64748b" Margin="0,4,0,0"/>
                </StackPanel>
                <Border Grid.Column="1" Background="#0369a1" CornerRadius="4" Padding="8,4" VerticalAlignment="Center">
                    <TextBlock Text="Release Oficial" FontSize="11" FontWeight="Bold" Foreground="#ffffff"/>
                </Border>
            </Grid>
        </Border>

        <!-- PATH SELECTION -->
        <StackPanel Grid.Row="1" Margin="0,0,0,16">
            <TextBlock Text="Diretório de Instalação:" FontSize="13" FontWeight="SemiBold" Foreground="#cbd5e1" Margin="0,0,0,6"/>
            <Grid>
                <Grid.ColumnDefinitions>
                    <ColumnDefinition Width="*"/>
                    <ColumnDefinition Width="Auto"/>
                </Grid.ColumnDefinitions>
                <TextBox Name="TxtPath" Grid.Column="0" Text="$DefaultInstallPath" Height="34" 
                         Background="#1e293b" Foreground="#f8fafc" BorderBrush="#334155" BorderThickness="1" 
                         VerticalContentAlignment="Center" Padding="8,0" FontSize="13">
                    <TextBox.Resources>
                        <Style TargetType="{x:Type Border}">
                            <Setter Property="CornerRadius" Value="6"/>
                        </Style>
                    </TextBox.Resources>
                </TextBox>
                <Button Name="BtnBrowse" Grid.Column="1" Content="Procurar..." Margin="8,0,0,0" Width="90" Height="34"/>
            </Grid>
        </StackPanel>

        <!-- OPTIONS LIST -->
        <Border Grid.Row="2" Background="#0f172a" BorderBrush="#1e293b" BorderThickness="1" CornerRadius="8" Padding="14" Margin="0,0,0,16">
            <ScrollViewer VerticalScrollBarVisibility="Auto">
                <StackPanel>
                    <TextBlock Text="Componentes da Instalação:" FontSize="13" FontWeight="Bold" Foreground="#38bdf8" Margin="0,0,0,8"/>
                    
                    <CheckBox Name="ChkCore" Content="Compilador Core (bin/hphlc.exe) e Runtime C" IsChecked="True" IsEnabled="False"/>
                    <TextBlock Text="  • Compilador nativo x64/LLVM e coletor de lixo Immix de baixa latência." FontSize="11" Foreground="#64748b" Margin="20,-2,0,6"/>

                    <CheckBox Name="ChkPath" Content="Adicionar HP-HL ao PATH do usuário" IsChecked="True"/>
                    <TextBlock Text="  • Permite executar 'hphlc' a partir de qualquer terminal ou script." FontSize="11" Foreground="#64748b" Margin="20,-2,0,6"/>

                    <CheckBox Name="ChkHome" Content="Configurar variável de ambiente HPHL_HOME" IsChecked="True"/>
                    <TextBlock Text="  • Facilita a localização automática do SDK por ferramentas e scripts." FontSize="11" Foreground="#64748b" Margin="20,-2,0,6"/>

                    <CheckBox Name="ChkVscode" Content="Instalar Extensão Oficial para Visual Studio Code e Cursor" IsChecked="True"/>
                    <TextBlock Text="  • Suporte a LSP, depuração nativa (DAP), realce de sintaxe e snippets." FontSize="11" Foreground="#64748b" Margin="20,-2,0,6"/>

                    <CheckBox Name="ChkStdlib" Content="Biblioteca Padrão (stdlib) e Cabeçalhos C de Runtime" IsChecked="True"/>
                    <TextBlock Text="  • Módulos std.io, std.math, std.net, std.sync e headers de FFI v2." FontSize="11" Foreground="#64748b" Margin="20,-2,0,6"/>

                    <CheckBox Name="ChkExamples" Content="Exemplos Práticos e Benchmarks (Física, Matrizes, Raytracer)" IsChecked="True"/>
                    <TextBlock Text="  • Códigos-fonte completos demonstrativos para teste imediato de performance." FontSize="11" Foreground="#64748b" Margin="20,-2,0,6"/>

                    <CheckBox Name="ChkDocs" Content="Documentação Oficial Completa Offline em Markdown" IsChecked="True"/>
                    <TextBlock Text="  • Manuais de sintaxe, guia de performance e referência de FFI v2." FontSize="11" Foreground="#64748b" Margin="20,-2,0,6"/>
                </StackPanel>
            </ScrollViewer>
        </Border>

        <!-- PROGRESS & STATUS -->
        <StackPanel Grid.Row="3" Margin="0,0,0,16">
            <ProgressBar Name="ProgBar" Height="10" Minimum="0" Maximum="100" Value="0" Background="#1e293b" Foreground="#38bdf8" BorderThickness="0">
                <ProgressBar.Resources>
                    <Style TargetType="{x:Type Border}">
                        <Setter Property="CornerRadius" Value="5"/>
                    </Style>
                </ProgressBar.Resources>
            </ProgressBar>
            <TextBlock Name="TxtStatus" Text="Pronto para iniciar a instalação." FontSize="12" Foreground="#94a3b8" Margin="0,6,0,0"/>
        </StackPanel>

        <!-- FOOTER BUTTONS -->
        <Grid Grid.Row="4">
            <Grid.ColumnDefinitions>
                <ColumnDefinition Width="Auto"/>
                <ColumnDefinition Width="*"/>
                <ColumnDefinition Width="Auto"/>
            </Grid.ColumnDefinitions>
            
            <Button Name="BtnDocs" Grid.Column="0" Content="Documentação Web" Background="#0f172a" BorderBrush="#334155"/>

            <StackPanel Grid.Column="2" Orientation="Horizontal">
                <Button Name="BtnCancel" Content="Cancelar" Margin="0,0,8,0" Width="90"/>
                <Button Name="BtnInstall" Content="Instalar Agora" Background="#0284c7" BorderBrush="#38bdf8" Foreground="#ffffff" Width="120"/>
            </StackPanel>
        </Grid>
    </Grid>
</Window>
"@

$reader = (New-Object System.Xml.XmlNodeReader $xaml)
$window = [System.Windows.Markup.XamlReader]::Load($reader)

# Elementos da UI
$txtPath = $window.FindName("TxtPath")
$btnBrowse = $window.FindName("BtnBrowse")
$chkPath = $window.FindName("ChkPath")
$chkHome = $window.FindName("ChkHome")
$chkVscode = $window.FindName("ChkVscode")
$chkStdlib = $window.FindName("ChkStdlib")
$chkExamples = $window.FindName("ChkExamples")
$chkDocs = $window.FindName("ChkDocs")
$progBar = $window.FindName("ProgBar")
$txtStatus = $window.FindName("TxtStatus")
$btnInstall = $window.FindName("BtnInstall")
$btnCancel = $window.FindName("BtnCancel")
$btnDocs = $window.FindName("BtnDocs")

# Ação: Procurar pasta
$btnBrowse.Add_Click({
    $fbd = New-Object System.Windows.Forms.FolderBrowserDialog
    $fbd.Description = "Selecione o diretório para instalação do HP-HL SDK"
    $fbd.SelectedPath = $txtPath.Text
    if ($fbd.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
        $txtPath.Text = $fbd.SelectedPath
    }
})

# Ação: Abrir Docs Web
$btnDocs.Add_Click({
    [System.Diagnostics.Process]::Start("https://hphl.dev")
})

# Ação: Cancelar
$btnCancel.Add_Click({
    $window.Close()
})

# Ação: Instalar
$btnInstall.Add_Click({
    $targetDir = $txtPath.Text.Trim()
    if ([string]::IsNullOrWhiteSpace($targetDir)) {
        [System.Windows.MessageBox]::Show("Por favor, selecione um diretório de instalação válido.", "HP-HL Setup", "OK", "Warning")
        return
    }

    $btnInstall.IsEnabled = $false
    $btnBrowse.IsEnabled = $false
    $txtPath.IsEnabled = $false
    $btnCancel.IsEnabled = $false

    try {
        # 1. Criar destino
        $txtStatus.Text = "Criando diretório de instalação..."
        $progBar.Value = 10
        [System.Windows.Forms.Application]::DoEvents()

        if (-not (Test-Path $targetDir)) {
            New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
        }

        # 2. Copiar bin/hphlc.exe
        $txtStatus.Text = "Copiando compilador nativo..."
        $progBar.Value = 25
        [System.Windows.Forms.Application]::DoEvents()

        $targetBin = Join-Path $targetDir "bin"
        New-Item -ItemType Directory -Path $targetBin -Force | Out-Null
        
        $srcExe = if (Test-Path (Join-Path $SdkRootDir "bin/hphlc.exe")) {
            Join-Path $SdkRootDir "bin/hphlc.exe"
        } else {
            Join-Path $SdkRootDir "compiler/bin/hphlc.exe"
        }
        Copy-Item $srcExe -Destination (Join-Path $targetBin "hphlc.exe") -Force

        # Copiar Runtime e Versão
        $targetRuntime = Join-Path $targetDir "runtime"
        New-Item -ItemType Directory -Path $targetRuntime -Force | Out-Null
        if (Test-Path (Join-Path $SdkRootDir "runtime/VERSION")) {
            Copy-Item (Join-Path $SdkRootDir "runtime/VERSION") -Destination $targetRuntime -Force
        }
        if (Test-Path (Join-Path $SdkRootDir "runtime/include")) {
            Copy-Item (Join-Path $SdkRootDir "runtime/include") -Destination $targetRuntime -Recurse -Force
        }

        # 3. Stdlib (Opcional)
        if ($chkStdlib.IsChecked) {
            $txtStatus.Text = "Copiando Biblioteca Padrão (stdlib)..."
            $progBar.Value = 45
            [System.Windows.Forms.Application]::DoEvents()

            $srcStdlib = Join-Path $SdkRootDir "stdlib"
            if (Test-Path $srcStdlib) {
                Copy-Item $srcStdlib -Destination $targetDir -Recurse -Force
            }
        }

        # 4. Exemplos (Opcional)
        if ($chkExamples.IsChecked) {
            $txtStatus.Text = "Copiando exemplos e benchmarks..."
            $progBar.Value = 60
            [System.Windows.Forms.Application]::DoEvents()

            $targetEx = Join-Path $targetDir "examples"
            New-Item -ItemType Directory -Path $targetEx -Force | Out-Null
            
            $srcEx = Join-Path $SdkRootDir "examples"
            if (Test-Path $srcEx) {
                Copy-Item $srcEx -Destination $targetDir -Recurse -Force
            }
        }

        # 5. Documentação (Opcional)
        if ($chkDocs.IsChecked) {
            $txtStatus.Text = "Copiando documentação oficial..."
            $progBar.Value = 75
            [System.Windows.Forms.Application]::DoEvents()

            $srcDocs = Join-Path $SdkRootDir "docs"
            if (Test-Path $srcDocs) {
                Copy-Item $srcDocs -Destination $targetDir -Recurse -Force
            }
        }

        # 6. Variáveis de Ambiente (HPHL_HOME e PATH)
        $progBar.Value = 85
        if ($chkHome.IsChecked) {
            $txtStatus.Text = "Configurando variável HPHL_HOME..."
            [Environment]::SetEnvironmentVariable("HPHL_HOME", $targetDir, "User")
        }
        if ($chkPath.IsChecked) {
            $txtStatus.Text = "Adicionando HP-HL ao PATH do usuário..."
            $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
            if ($userPath -notlike "*$targetBin*") {
                $newPath = "$targetBin;$userPath"
                [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
            }
        }

        # 7. Extensão do VS Code / Cursor (Opcional)
        if ($chkVscode.IsChecked) {
            $txtStatus.Text = "Instalando extensão para VS Code e Cursor..."
            $progBar.Value = 95
            [System.Windows.Forms.Application]::DoEvents()

            $vsix = Join-Path $SdkRootDir "editors/vscode/hphl-1.0.0.vsix"
            if (-not (Test-Path $vsix)) {
                $vsix = Join-Path $SdkRootDir "editors/hphl-1.0.0.vsix"
            }
            if (Test-Path $vsix) {
                if (Get-Command "code" -ErrorAction SilentlyContinue) {
                    & code --install-extension $vsix --force 2>$null
                }
                if (Get-Command "cursor" -ErrorAction SilentlyContinue) {
                    & cursor --install-extension $vsix --force 2>$null
                }
            }
        }

        # 8. Sucesso Final
        $progBar.Value = 100
        $txtStatus.Text = "Instalação concluída com sucesso!"
        $txtStatus.Foreground = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.Color]::FromRgb(16, 185, 129))

        $btnCancel.Content = "Fechar"
        $btnCancel.IsEnabled = $true
        $btnInstall.Content = "Abrir Terminal"
        $btnInstall.IsEnabled = $true
        $btnInstall.Background = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.Color]::FromRgb(16, 185, 129))
        $btnInstall.BorderBrush = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.Color]::FromRgb(52, 211, 153))

        $btnInstall.Add_Click({
            Start-Process "powershell.exe" -ArgumentList "-NoExit", "-Command", "Write-Host 'HP-HL SDK v1.0.0 pronto!'; hphlc --version"
            $window.Close()
        })

    } catch {
        $txtStatus.Text = "Erro durante a instalação: $($_.Exception.Message)"
        $txtStatus.Foreground = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.Color]::FromRgb(239, 68, 68))
        $btnCancel.IsEnabled = $true
        $btnInstall.IsEnabled = $true
    }
})

$window.ShowDialog() | Out-Null
