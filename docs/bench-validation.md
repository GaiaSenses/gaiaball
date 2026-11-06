# Roteiro de validação em bancada — Gaiaball v2

Este roteiro acompanha o checklist da seção 7 do relatório interativo
(`relatorio/index.html`) e usa o mesmo número de itens e a mesma ordem. Nada do firmware v2
foi medido em hardware até aqui: taxas e estabilidade são **metas de projeto** a confirmar.

Metas de aceitação (spec `docs/superpowers/specs/2026-11-06-gaiaball-v2-design.md`):
notify de orientação 50–60 Hz, latência medida < 50 ms, 24 h sem travar, zero NaN.

## 1. Pré-requisitos

- Placa ESP32 (DevKit) montada conforme o pinout de `firmware/README.md`: IMU ICM-20948 em
  SDA 21 / SCL 22, INMP441 em WS 25 / SCK 26 / SD 33, sensor de CO₂ com TX no GPIO 16,
  potenciômetro no GPIO 34, LED de som no GPIO 2 e LED de CO₂ no GPIO 4.
- Cabo USB de dados, computador com PlatformIO (`pio`) **ou** `arduino-cli`.
- Chrome ou Edge (Web Bluetooth) para o config-app.
- Uma superfície plana e um celular/ímã pequeno para o teste de deriva.
- PIN de fábrica: `1234`.

Toolchain fixo: ESP32 Arduino core **2.0.17** e NimBLE-Arduino **1.4.3** (não use NimBLE 2.x
nem core 3.x).

## 2. Gravar o firmware

PlatformIO (recomendado), a partir da raiz do repositório:

```sh
pio test -d firmware -e native                 # opcional: 66 testes no host
pio run -d firmware -e esp32dev -t upload      # compila e grava (detecta a porta)
pio run -d firmware -e esp32dev -t upload --upload-port /dev/ttyUSB0   # porta explícita
pio device monitor -d firmware -e esp32dev     # log serial, 115200 baud
```

arduino-cli:

```sh
arduino-cli core install esp32:esp32@2.0.17
arduino-cli lib install NimBLE-Arduino@1.4.3
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/Gaiaball
arduino-cli upload  --fqbn esp32:esp32:esp32 -p /dev/ttyUSB0 firmware/Gaiaball
```

Arduino IDE 2.x: ver `firmware/README.md` (Boards Manager 2.0.17, NimBLE-Arduino 1.4.3).

**Critério (item 01):** no monitor serial, cada serviço (IMU, áudio, CO₂, BLE, NVS) reporta
status no boot, sem erro. O dispositivo anuncia como `Globo_1`.

## 3. Bancada com o config-app

```sh
cd config-app
npm install
npm run dev        # abre em http://localhost:5173 (use Chrome/Edge)
```

O roteiro básico do app está em [`config-app/README.md`](../config-app/README.md); aqui só
os critérios e onde olhar cada número.

1. **Conectar** (painel Conexão) e escolher `Globo_1`.
2. **Desbloquear** com o PIN `1234` (botão "Desbloquear"). O firmware não confirma o PIN:
   se escritas/comandos forem ignorados, o PIN está errado ou o bloqueio de 30 s está ativo.
3. **Ler do dispositivo** (painel Configuração). O formulário deve vir com os padrões da
   tabela de `firmware/README.md` (item 02).
4. Medir no painel **Métricas** (deixe a bola em movimento):

| Critério | Onde olhar | Meta |
|---|---|---|
| Taxa de orientação (item 03) | Métricas › **Taxa** | ≥ 50 Hz (com `imuNotifyHz` = 50 ou mais) |
| Perda (item 04) | Métricas › **Perda** | ≈ 0 % durante 10 min (contada por buracos no `seq`) |
| Jitter | Métricas › **Jitter** | anotar o valor observado |
| Idade da amostra | Métricas › **Idade da amostra** | estável, sem crescer |
| Sem NaN nem saltos (item 05) | vista 3D | girar rápido e com trancos por 5 min: o modelo não pode pular, congelar nem sumir |
| IMU interna | Firmware › **IMU** e **Notify** | próximos da taxa configurada |
| Saúde | Firmware › **Saúde** | IMU, áudio e CO₂ ok |

Se a taxa ficar abaixo do esperado, confira o campo **Taxa (Hz)** (faixa 10–60) e aplique.

## 4. Precisão de movimento

1. Com a bola na posição de referência e parada, clique **Calibrar montagem** (comando
   `0x14`, salvo automaticamente na NVS), depois **Zerar orientação** (`0x10`). Ambos exigem o
   desbloqueio (item 06).
2. Sobre uma **superfície plana**, gire a bola uma volta completa em cada eixo. No modelo 3D
   deve ser exatamente uma volta, no mesmo eixo e sentido (item 07).
3. Alterne o seletor **Vista** entre **Cru** e **Filtrado** e compare. Ajuste **Suavização**
   (0 = desligada) conforme o resultado e aplique.
4. Deriva (item 08): em modo **Quat6 (relativo)**, deixe a bola parada por 5 min e anote a
   deriva do yaw. Aproxime um ímã ou celular: em Quat6 nada deve mudar. Compare com
   **Quat9 (absoluto)**.
5. Para persistir ajustes, **Salvar no dispositivo** (`0x11`). "Aplicar" vale só até reiniciar.

## 5. CO₂: confirmar o protocolo do "SC8"

O protocolo do sensor não é confirmado; o parser assume quadro de 16 bytes, cabeçalho
`42 4D`, ppm nos bytes 6–7 (big-endian) e checksum no último byte (item 09).

1. Ligue o log bruto: no config-app, painel Configuração › CO₂ › **Log bruto** e **Aplicar**
   (equivale ao TLV `0x26` = 1; ver `firmware/docs/protocol.md`).
2. No monitor serial (`pio device monitor -d firmware -e esp32dev`, 115200) aparecem linhas
   como `raw: 42 4D 00 ...`, 16 bytes por linha (máximo de 4 linhas por ciclo e 20 linhas/s no total).
3. Confira:
   - O quadro começa com `42 4D`? Se não, o protocolo é outro e o parser precisa ser ajustado.
   - Quantos bytes entre dois `42 4D` consecutivos? Deve ser 16.
   - Os bytes 6 e 7 formam o ppm esperado (`(b6 << 8) | b7`)? Compare com um valor conhecido
     (ar externo ~420 ppm, ou respire no sensor para subir).
   - **Qual checksum bate.** Some os bytes em módulo 256 (8 bits, descarte o excesso):
     - Variante **0** (padrão, TLV `0x21` = 0): soma dos bytes `0..13` (índices 0 a 13).
     - Variante **1** (legado, `0x21` = 1): soma dos bytes `0..14`.
     - Compare o resultado com o **último byte** (índice 15). A variante que igualar é a certa.
     Exemplo de conta: some os 14 (ou 15) primeiros bytes em hexadecimal e fique só com os
     dois dígitos finais.
4. Se a variante for a 1, mude **Variante de checksum** para "Variante 1" e **Aplicar**. Confirme
   que o CO₂ em Firmware › **CO₂** passa a ser válido e que **Erros UART** para de crescer.
5. **Salvar no dispositivo** (`0x11`) e **desligar o Log bruto** (volta a `0x26` = 0, para não
   poluir o serial). Aplique e salve de novo.

Também verifique o CO₂ virtual (item 10): uma palma a ~1 m faz o LED de som piscar na hora; o
CO₂ virtual (2100 ppm) chega ~1 s depois e dura ~17 s. A taxa da IMU não pode cair.

## 6. Teste simultâneo: site + config-app

O firmware aceita duas centrais ao mesmo tempo (item 11).

1. Conecte o config-app e meça a perda como na seção 3.
2. Em outra aba/janela (ou outro computador) abra o site Gaiasenses com o branch
   `feature/gaiaball-binary-protocol` e conecte ao mesmo `Globo_1`.
3. O globo do site responde enquanto o config-app continua medindo. **Nenhum dos dois pode
   cair**, e a perda no config-app deve continuar ≈ 0 %.

## 7. Teste de 24 h

Item 14. Com o config-app (ou o site) conectado, deixe o globo ligado por 24 h, em movimento
periódico e com alguns picos de som. Anote no início, após 1 h e no fim, no painel Firmware:

| Campo | Aceitação |
|---|---|
| **Heap livre / mín** | o mínimo estabiliza (sem queda contínua = sem vazamento) |
| **FIFO resyncs** | não cresce de forma contínua (alguns são aceitáveis) |
| **Erros I²S** e **Erros UART** | não crescem |
| **Taxa** e **Perda** | continuam nas metas da seção 3 |

Critérios da spec: 24 h sem travar, **sem reinício** (o uptime só cresce; se zerar, houve
reboot), zero NaN. Se a conexão cair, reconecte e anote a hora: diferencie queda de BLE (uptime
segue) de reboot (uptime volta a zero).

## 8. Outros testes de bancada

- **Bloqueio do PIN (item 12):** 5 PINs errados seguidos recusam o desbloqueio por 30 s, mesmo
  com o PIN certo. Desconectar e reconectar limpa a contagem.
- **Trocar o PIN (item 15):** desbloqueie, grave o novo PIN (campo **Novo PIN**, TLV `0x41`) e
  use **Salvar no dispositivo** (`0x11`). Sem salvar, o PIN antigo volta após reiniciar. Se
  esquecer o PIN: `pio run -d firmware -e esp32dev -t erase` e regrave.
- **Ligar sem a IMU (item 13):** desconecte a IMU e reinicie. O boot demora cerca de 5 s a
  mais, o BLE sobe, o LED de erro acende e o diagnóstico mostra a flag `imuOk` desligada
  (Firmware › **Saúde**).

## 9. Troubleshooting

| Sintoma | Causa provável / ação |
|---|---|
| IMU ausente: boot lento (+5 s) e LED de erro aceso | IMU não responde no I²C (fiação SDA 21 / SCL 22, AD0 em GND, alimentação). `imuOk` fica desligada; o resto do firmware segue rodando. |
| LED de som aceso fixo | O LED de som também sinaliza falha de IMU/áudio; veja o log serial e Firmware › **Saúde**. |
| Escritas e comandos ignorados | Não desbloqueado, PIN errado, ou bloqueio de 30 s (5 erros seguidos). Aguarde 30 s ou reconecte e tente de novo. |
| Valores voltam ao anterior após **Aplicar** | O app avisa "alguns valores não foram aceitos (desbloqueou com o PIN?)". |
| Config sumiu após reiniciar | Faltou **Salvar no dispositivo** (`0x11`). |
| Navegador não lista o dispositivo | Use Chrome/Edge em `localhost` ou HTTPS; confira se o nome BLE continua `Globo_1`. |
| Erros UART crescendo / CO₂ inválido | Variante de checksum errada (seção 5) ou fiação do GPIO 16. |
| Site novo não conecta no firmware antigo, ou o contrário | Veja a ordem de deploy no `README.md` da raiz. |
