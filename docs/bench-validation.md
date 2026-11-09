# Roteiro de validação em bancada — Gaiaball v2

Este roteiro acompanha o checklist da seção 7 do relatório interativo
(`relatorio/index.html`) e usa o mesmo número de itens e a mesma ordem. Nada do firmware v2
foi medido em hardware até aqui: taxas e estabilidade são **metas de projeto** a confirmar.

Metas de aceitação (spec `docs/superpowers/specs/2026-11-06-gaiaball-v2-design.md`):
notify de orientação 50–60 Hz, latência medida < 50 ms, 24 h sem travar, zero NaN.

## 1. Pré-requisitos

- Placa ESP32 (DevKit) montada conforme o pinout de `firmware/README.md`: IMU GY-91 (MPU-9250) em
  SDA 21 / SCL 22, INMP441 em WS 25 / SCK 26 / SD 33, sensor de CO₂ com TX no GPIO 16,
  potenciômetro no GPIO 34, LED de som no GPIO 2 (D2 na placa) e LED de CO₂ no GPIO 4
  (D1). 1 pino = 1 função (ver `firmware/docs/hardware-pinout.md`).
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

Além da vista 3D e das métricas, o painel **Sensores em tempo real** mostra cada sensor
com sua unidade (orientação °, aceleração g, microfones dBFS/dB SPL, CO₂ ppm,
potenciômetro %) e gráficos de linha com eixo X (tempo) compartilhado — útil para ver,
por exemplo, o pico de áudio cruzando o limiar quando se bate palma, e a leitura do SC8
mudando ao respirar perto do sensor.

**Ajuste do passa-banda do sopro:** com a bola conectada, sopre sobre ela olhando o
**Espectro do áudio (cru)** no painel Sensores. As frequências excitadas pelo sopro
sobem no gráfico; mova o **High-pass** e o **Low-pass** (Configuração › Áudio) até a
banda cobrir o que o sopro excita e cortar o que não interessa (voz, música ambiente).
Aplicar já grava. Teste também taxas de orientação maiores (Configuração › Orientação ›
Taxa, até 200 Hz) observando **Taxa** e **Perda** nas métricas.

O roteiro básico do app está em [`config-app/README.md`](../config-app/README.md); aqui só
os critérios e onde olhar cada número.

1. **Conectar** (painel Conexão) e escolher `Globo_1`.
2. **Desbloquear** com o PIN `1234` (botão "Desbloquear"). O firmware não confirma o PIN:
   se escritas/comandos forem ignorados, o PIN está errado ou o bloqueio de 30 s está ativo.
3. **Ler do dispositivo** (painel Configuração). O formulário já abre preenchido com os
   padrões de fábrica; depois da leitura os valores devem continuar iguais aos da tabela
   de `firmware/README.md` (item 02). O painel tem duas abas: **Básico** (14 parâmetros
   de operação) e **Avançado** (25 de afinação da cadeia de sinais). A contagem na aba
   Avançado acende quando algum parâmetro está fora do padrão — confira isso antes de
   culpar o firmware por um comportamento estranho.
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
3. Se o movimento estiver tremido, ajuste a suavização em Configuração › **Básico** ›
   *Suavização da saída › Modo* (e, se escolher o filtro 1€, os dois parâmetros dele
   ficam em **Avançado**) e aplique: a vista 3D mostra o quaternion exatamente como chega da bola,
   então o efeito do slerp — e o atraso que ele adiciona — aparece na própria vista.
   Lembre que o site também suaviza por conta própria (mediana + EMA); evite exagerar.
4. Deriva (item 08): em modo **Quat6 (relativo)**, deixe a bola parada por 5 min e anote a
   deriva do yaw. Aproxime um ímã ou celular: em Quat6 nada deve mudar. Compare com
   **Quat9 (absoluto)**.

## 4.1 Pipeline do IMU (firmware v7)

Tudo aqui se observa em Métricas › Firmware, que agora publica o estado interno da fusão.

| Item | Como verificar | Esperado |
|---|---|---|
| Bias do giro aprende | Deixe a bola parada sobre a mesa | Em ~1 s aparece **em repouso**, depois **aprendendo**; em ~15 s o valor de “Bias do giroscópio” estabiliza e a deriva de yaw em Quat6 cai |
| Bias com offset grande | Compare o bias estimado com a leitura crua do giroscópio parado (log serial) | O firmware aprende o offset mesmo acima de 2 °/s — era exatamente o caso que a versão anterior nunca corrigia |
| Rejeição de aceleração | Dê um tranco lateral na bola, mantendo-a nivelada | “Estado da fusão” mostra **accel ignorado** durante o tranco e o modelo 3D não tomba |
| Recuperação | Mantenha a bola acelerada/inclinada por mais de 5 s | O selo **accel ignorado** desaparece (o firmware volta a confiar no sensor) |
| Rejeição magnética (Quat9) | Aproxime um ímã do globo | “Estado da fusão” mostra **mag ignorado** e o rumo não gira atrás do ímã |
| Saturação do giroscópio | Gire a bola o mais rápido que conseguir | Com faixa ±1000 °/s o selo **saturação** deve ser raro; se aparecer com frequência, suba para ±2000 |
| Anti-serrilhamento | Em “Filtro anti-serrilhamento (DLPF)” deixe em automático e apoie a bola sobre algo que vibre | Menos tremor do que forçando 41 Hz a 50 Hz de taxa |
| Filtro 1€ | Modo **filtro 1€**; compare com **slerp fixo** em 100 | Parada: ambos ficam quietos. Em movimento rápido: o 1€ acompanha, o slerp fixo “borracha” (~190 ms) |
| Espícula da aceleração | Ligue o filtro de espícula (k 3,0) e observe o gráfico de aceleração | Picos isolados desaparecem sem atraso visível; movimento real passa intacto |

## 4.2 Calibração do magnetômetro (painel Magnetômetro)

1. Desbloqueie com o PIN e afaste a bola de metal, caixas de som e computadores.
2. **Iniciar calibração** e gire devagar desenhando um 8 até os **8 setores** acenderem.
3. Confira: campo entre 22 e 28 µT (Brasil) e resíduo **abaixo de 3 %**.
4. **Gravar calibração na bola** (já persiste na NVS) — em Métricas a linha “Calibração
   magnética” passa a **calibrado**.
5. Em Quat9, gire a bola 360° sobre a mesa: o rumo deve voltar ao mesmo valor
   (±5°). Antes da calibração esse erro chega a dezenas de graus.
6. **Remover calibração** volta ao magnetômetro cru, para comparar.
5. Para persistir ajustes, **Salvar no dispositivo** (`0x11`). "Aplicar" vale só até reiniciar.

## 5. CO₂: validar o SC8-CO2-5K

O sensor da placa é um **SC8-CO2-5K** (Shenzhen Shenchen Technology, vendido como
"YYS SC8"; o valor `Sensor_CO2_SC8` do esquemático é literal). Ele **transmite sozinho**
um quadro de 16 bytes `42 4D ...` por segundo (9600 8N1); o firmware v2 apenas escuta —
nunca transmite, porque **qualquer consulta desliga a saída ativa do SC8 até religar a
alimentação** (datasheet SC8-CO2-5K V1.4, seção 8). Detalhes e o quadro-dourado em
`firmware/docs/protocol.md` e `firmware/docs/hardware-pinout.md`.

1. Com o firmware v2 gravado, confira em Firmware › **CO₂** no config-app: o valor deve
   ficar **válido** em até ~30 s após o boot (o SC8 precisa aquecer ~8–25 s) e acompanhar
   o ambiente (ar externo ~420 ppm; respire perto do sensor para subir — faixa útil
   começa em 400 ppm).
2. Se o CO₂ ficar sempre "stale": ligue o **Log bruto** (CO₂ › Log bruto › **Aplicar**,
   TLV `0x26` = 1) e abra o monitor serial (`pio device monitor -d firmware -e esp32dev`,
   115200):
   - Linhas `raw: 42 4D ...` a cada ~1 s → o sensor transmite; valide um quadro à mão:
     some os bytes `0..14` módulo 256 e compare com o byte 15 (ex. do datasheet:
     `42 4D 0C 51 09 A2 07 2B 01 35 05 81 20 08 00 AD` → soma `AD`, ppm `07 2B` = 1835).
     Se **Erros UART** cresce com o sensor transmitindo, verifique interferência/fiação
     no GPIO16.
   - Nenhuma linha `raw:` → o sensor não transmite. Confira a alimentação de **5 V**
     (pico de 150 mA) e a fiação SC8 TXD → GPIO16. Lembre: se o sensor recebeu alguma
     consulta (firmware antigo com Modbus, adaptador serial, etc.), a saída ativa fica
     muda até **religar a alimentação** — desligue e ligue a bola.
3. Ao final, **Salvar no dispositivo** (`0x11`) e **desligar o Log bruto** (volta a
   `0x26` = 0). Aplique e salve de novo.

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
  mais, o BLE sobe e o diagnóstico mostra a flag `imuOk` desligada
  (Firmware › **Saúde**).

## 9. Troubleshooting

| Sintoma | Causa provável / ação |
|---|---|
| IMU ausente: boot lento (+5 s) | IMU não responde no I²C (fiação SDA 21 / SCL 22, AD0 em GND, alimentação). `imuOk` fica desligada (Firmware › **Saúde**); o resto do firmware segue rodando. |
| Escritas e comandos ignorados | Não desbloqueado, PIN errado, ou bloqueio de 30 s (5 erros seguidos). Aguarde 30 s ou reconecte e tente de novo. |
| Valores voltam ao anterior após **Aplicar** | O app avisa "alguns valores não foram aceitos (desbloqueou com o PIN?)". |
| Config sumiu após reiniciar | Faltou **Salvar no dispositivo** (`0x11`). |
| Navegador não lista o dispositivo | Use Chrome/Edge em `localhost` ou HTTPS; confira se o nome BLE continua `Globo_1`. |
| Erros UART crescendo / CO₂ inválido | Fiação/interferência no GPIO16, ou o SC8 emudeceu após receber uma consulta — religue a alimentação (seção 5). |
| Site novo não conecta no firmware antigo, ou o contrário | Veja a ordem de deploy no `README.md` da raiz. |
