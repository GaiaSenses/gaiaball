# Gaiaball Config

App web (Vite + TypeScript) para configurar e testar o Gaiaball v2 via Web Bluetooth: visualização 3D da orientação (cru × filtrado), métricas do fluxo (taxa, perda, jitter), parâmetros do firmware, presets e um painel de **sensores em tempo real** com gráficos de linha (Plotly.js).

## Sensores em tempo real

O painel "Sensores em tempo real" mostra o valor atual de cada sensor com a unidade
correta e, abaixo, gráficos de linha empilhados verticalmente que **compartilham o
mesmo eixo X (tempo, em segundos)** — janela de 120 s amostrada a 10 Hz, cada gráfico
com uma cor própria. Arraste para dar zoom; duplo clique restaura.

| Sensor | Unidade | Derivação |
|---|---|---|
| Orientação (IMU GY-91/MPU-9250) | graus (°) | fusão Madgwick no firmware → quaternion → Euler na convenção legada (mesma saída que o site entrega ao patch Pd) |
| Aceleração (IMU) | g | contagens do MPU-9250 ÷ 8192 (fundo de escala ±4 g) |
| Microfones esq./dir. (INMP441) | dBFS (AES17) + dB SPL estimado | RMS em contagens de 24 bits (I²S 32 bits, `>>8`; fundo de escala 2²³); 0 dBFS = seno de fundo de escala; SPL ≈ dBFS + 120 (sensibilidade do INMP441: −26 dBFS @ 94 dB SPL, 1 kHz) |
| CO₂ (Senseair S8) | ppm | leitura Modbus do firmware (EMA opcional, TLV `0x28`) |
| Potenciômetro | % do curso | ADC filtrado 0–4095 → 0–100 % (o limiar mapeado aparece no cartão e como linha pontilhada nos gráficos dos microfones) |

Os dados de áudio/potenciômetro chegam num pacote próprio (`SensorsPacket`, 5 Hz, na
característica de diagnóstico — ver `firmware/docs/protocol.md`); IMU e CO₂ usam os
pacotes já existentes. No modo demo o painel roda com dados sintéticos.

### Analisador de espectro (ajuste do passa-banda do sopro)

A bola transmite o **áudio bruto decimado** (mono, 2 kHz, int16, ~4,4 kB/s) e o
**FFT roda aqui no navegador** (512 pontos, janela Hann de 256 ms, bins de ~3,9 Hz,
0–1000 Hz). Sopre na bola e observe quais frequências sobem; as linhas tracejadas
mostram os cortes HP/LP configurados (ecoados pelo firmware em cada pacote). Os
filtros (HP 10 Hz / LP 200 Hz, Q 5,00 por padrão) são aplicados **no firmware**,
antes do detector de pico — o espectro mostra o sinal **cru**, justamente para
escolher os cortes. Atenção: Q > 0,71 cria um pico ressonante de ~20·log₁₀(Q) dB
exatamente no corte; a inclinação da atenuação vem da ordem do filtro, não do Q.

### Persistência

**Aplicar** envia só os campos alterados, confere a aceitação relendo o dispositivo
e, se tudo foi aceito, **já grava na NVS** (comando `0x11`): as configurações feitas
pela UI sobrevivem a desligar/reiniciar a bola.

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

**Modo demo:** abra `http://localhost:5173/?demo=1` para ver a visualização 3D, as métricas e o painel de sensores com dados sintéticos, sem dispositivo.

## Roteiro de bancada

1. **Conectar** (painel Conexão) e escolher o Gaiaball na lista do navegador.
2. **Desbloquear** com o PIN (padrão de fábrica: `1234`). O firmware não confirma o PIN; sem ele, comandos e escritas sensíveis são ignorados.
3. **Ler do dispositivo** (painel Configuração) para preencher o formulário.
4. Em **superfície plana**, gire a bola e acompanhe **Taxa**, **Perda** e **Jitter** no painel Métricas.
5. Compare **Cru × Filtrado** na vista 3D (seletor "Vista") e ajuste Modo, Taxa (Hz) e Suavização.
6. **Aplicar**: só os campos alterados são enviados; em seguida o app relê o dispositivo para verificar. Se algum valor voltar ao anterior, aparece o aviso "alguns valores não foram aceitos (desbloqueou com o PIN?)".
7. **Salvar no dispositivo** (painel Conexão) para persistir. "Aplicar" vale somente até reiniciar.
8. **Presets**: salve a configuração atual com um nome, carregue (só preenche o formulário; clique em Aplicar para enviar), exclua, exporte/importe JSON. O PIN nunca entra nos presets nem é lido do dispositivo.
