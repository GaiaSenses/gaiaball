# Gaiaball Config

App web (Vite + TypeScript) para configurar e testar o Gaiaball v2 via Web Bluetooth: visualização 3D da orientação (cru × filtrado), métricas do fluxo (taxa, perda, jitter), parâmetros do firmware e presets.

## Requisitos

- Chrome ou Edge (Web Bluetooth não existe no Firefox/Safari).
- Servir em `localhost` ou HTTPS (requisito do Web Bluetooth).
- Node.js e npm.

## Uso

```bash
npm install
npm run dev        # servidor de desenvolvimento (localhost)
npm run build      # gera dist/
npm run preview    # serve dist/ localmente (ou sirva dist/ com qualquer servidor estático)
npm test           # testes (vitest)
```

**Modo demo:** abra `http://localhost:5173/?demo=1` para ver a visualização 3D com rotação sintética, sem dispositivo.

## Roteiro de bancada

1. **Conectar** (painel Conexão) e escolher o Gaiaball na lista do navegador.
2. **Desbloquear** com o PIN (padrão de fábrica: `1234`). O firmware não confirma o PIN; sem ele, comandos e escritas sensíveis são ignorados.
3. **Ler do dispositivo** (painel Configuração) para preencher o formulário.
4. Em **superfície plana**, gire a bola e acompanhe **Taxa**, **Perda** e **Jitter** no painel Métricas.
5. Compare **Cru × Filtrado** na vista 3D (seletor "Vista") e ajuste Modo, Taxa (Hz) e Suavização.
6. **Aplicar**: só os campos alterados são enviados; em seguida o app relê o dispositivo para verificar. Se algum valor voltar ao anterior, aparece o aviso "alguns valores não foram aceitos (desbloqueou com o PIN?)".
7. **Salvar no dispositivo** (painel Conexão) para persistir. "Aplicar" vale somente até reiniciar.
8. **Presets**: salve a configuração atual com um nome, carregue (só preenche o formulário; clique em Aplicar para enviar), exclua, exporte/importe JSON. O PIN nunca entra nos presets nem é lido do dispositivo.
