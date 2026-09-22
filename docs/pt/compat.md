# Política de Compatibilidade e Versionamento

O projeto HP-HL segue estritamente o Versionamento Semântico (**SemVer 2.0.0**) para garantir estabilidade e previsibilidade aos desenvolvedores e estúdios que utilizam a linguagem em produção.

---

## Garantias de Estabilidade

- **Compatibilidade de ABI:** Programas compilados na versão `1.0.x` mantêm compatibilidade binária com atualizações do runtime `1.x.y`.
- **Compatibilidade de Código-Fonte:** Códigos válidos na especificação v1.0.0 continuarão compilando sem erros em todas as versões `1.x.x`.
- **Mudanças Incompatíveis:** Reservadas exclusivamente para futuras versões principais (Major bump, ex: `2.0.0`), acompanhadas de guias de migração detalhados.
