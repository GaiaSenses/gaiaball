# pd-gaiaball — objeto `[gaiaball]` para Pure Data

Acesso de **leitura** a todos os dados da Gaiaball dentro do Pure Data, via
Bluetooth Low Energy, nos três sistemas (Windows, Linux e macOS). Nada é
escrito na bola: o objeto só escuta.

```
┌──────────┐   BLE (notify)   ┌─────────────────────┐   UDP/FUDI (localhost)   ┌────────────┐
│ Gaiaball │ ───────────────> │ gaiaball_bridge.py  │ ───────────────────────> │ [gaiaball] │──> 9 outlets
│ (ESP32)  │                  │ (Python + bleak)    │       porta 9478         │  (Pd puro) │
└──────────┘                  └─────────────────────┘                          └────────────┘
```

## Por que uma ponte (e não um external compilado)?

O Pure Data não tem Bluetooth nativo. As alternativas reais são:

| Abordagem | Problema |
|---|---|
| External em C (como o [pd-witsensor](https://github.com/ben-wes/pd-witsensor), feito sobre SimpleBLE) | exige **binário compilado para cada OS** e um fork em C++ para falar o protocolo da Gaiaball; instalar deixa de ser "copiar uma pasta" |
| Web Bluetooth (como o config-app) | só funciona dentro de um navegador |
| **Abstração Pd vanilla + ponte Python (esta pasta)** | instala sem compilar nada; o lado Pd é 100 % vanilla (idêntico nos 3 OSes) e o lado BLE usa [bleak](https://github.com/hbldh/bleak), a biblioteca BLE multiplataforma mais madura que existe |

Se um dia valer a pena eliminar o Python, o caminho natural é um fork do
pd-witsensor/SimpleBLE decodificando o protocolo da Gaiaball — a abstração
`[gaiaball]` continuaria a mesma, trocando só o transporte.

## Conteúdo da pasta

| Arquivo | O que é |
|---|---|
| `gaiaball.pd` | a abstração — crie um objeto `[gaiaball]` no seu patch |
| `gaiaball-help.pd` | patch de ajuda/exemplo (clique-direito no objeto → Help, ou abra direto) |
| `gaiaball_bridge.py` | a ponte BLE → UDP (roda fora do Pd, em qualquer terminal) |
| `requirements.txt` | dependência Python (`bleak`) |

## Requisitos

- **Pure Data vanilla ≥ 0.51** (usa `[netreceive -u]`; nenhum external, nenhuma
  biblioteca do Deken).
- **Python ≥ 3.9** com o pacote `bleak`.
- Rádio **Bluetooth 4.0+** no computador.
- Por sistema:
  - **Windows 10 (1709+) / 11** — nada além do Python; use o Bluetooth embutido.
  - **macOS 10.15+** — na primeira execução o sistema pede permissão de
    Bluetooth para o Terminal (ou para o app Python). Conceda em
    Ajustes → Privacidade e Segurança → Bluetooth.
  - **Linux** — BlueZ ≥ 5.43 ativo (`bluetoothctl show` deve listar o adaptador).
    Não é preciso parear a bola; BLE por GATT não usa pareamento aqui.

## Instalação

1. **Copie esta pasta** (`pd-gaiaball/`) para onde preferir — por exemplo, para
   a pasta de externals do Pd (`~/Documents/Pd/externals/` no macOS/Windows,
   `~/.local/lib/pd/extra/` no Linux) ou junto do seu projeto.
2. **Diga ao Pd onde ela está** (qualquer uma das opções):
   - Pd → *File → Preferences → Path* → *New…* → selecione a pasta
     `pd-gaiaball/`; **ou**
   - coloque `[declare -path pd-gaiaball]` no seu patch (com a pasta ao lado
     do patch); **ou**
   - simplesmente salve seu patch dentro da própria pasta (o Pd sempre acha
     abstrações no diretório do patch aberto).
3. **Instale a dependência Python** (uma vez):

   ```sh
   # Windows (PowerShell ou cmd)
   py -m pip install -r requirements.txt

   # macOS / Linux
   python3 -m pip install -r requirements.txt
   ```

## Uso

1. Ligue a bola (ela anuncia como `Globo_1` por padrão).
2. Rode a ponte num terminal — e deixe rodando:

   ```sh
   # Windows
   py gaiaball_bridge.py

   # macOS / Linux
   python3 gaiaball_bridge.py
   ```

   A ponte procura a bola sozinha (pelo nome e pelo UUID do serviço BLE),
   conecta, e **reconecta sozinha** se a bola reiniciar ou sair do alcance.
3. No Pd, crie `[gaiaball]`. Pronto: os 9 outlets começam a emitir assim que a
   ponte conecta. Abra `gaiaball-help.pd` para ver tudo funcionando.

Opcional: `[gaiaball 9500]` escuta em outra porta UDP (rode a ponte com
`--port 9500`). O inlet aceita as mensagens do `[netreceive]` — por exemplo
`[listen 9500(` para trocar a porta em tempo real.

## Outlets (esquerda → direita)

| # | Nome | Conteúdo | Unidades |
|---|---|---|---|
| 1 | `euler` | `roll pitch yaw` | graus (°), **convenção legada — exatamente os mesmos ângulos que o site GaiaSenses entrega ao patch Pd** |
| 2 | `quat` | `w x y z` | quaternion unitário (adimensional) |
| 3 | `accel` | `x y z` | contagens cruas do MPU-9250 (±4 g): **g = valor / 8192** |
| 4 | `co2` | `ppm valido velho virtual idade` | ppm; 3 flags 0/1 (leitura real fresca / sensor sem quadro há >5 s / pulso de sopro ativo); idade em s (255 = nunca) |
| 5 | `mics` | `dBFS_L dBFS_R rms_L rms_R filtrado` | dBFS (AES17, 0 = senoide de fundo de escala; SPL ≈ dBFS + 120); RMS em contagens de 24 bits; flag 0/1 = RMS medido após o passa-banda do sopro |
| 6 | `pot` | `porcento cru limiar controla` | % do curso; ADC 0–4095; limiar em RMS (mesma escala dos mics); flag 0/1 = potenciômetro controla o limiar |
| 7 | `diag` | `uptime hzIMU hzNotify errosI2C errosI2S errosUART heapKb heapMinKb imuOk audioOk co2Ok` | s; Hz; contadores; KB; 3 flags 0/1 — chega 1×/s |
| 8 | `micenv` | `rms_L rms_R pico_L pico_R` | **dBFS**, medidos pelo firmware em janela de 20 ms, com o pico amostrado nos 16 kHz cheios. Chega **na taxa de orientação** (até 200 Hz) — é este o sinal para controle de sopro, não o `mics` de 5 Hz. RMS na referência AES17 (0 dBFS = senoide de fundo de escala), pico na referência de amplitude (0 dBFS = amostra de fundo de escala), portanto 3,01 dB abaixo para a mesma senoide |
| 9 | `status` | `bridge <versão>` · `scanning` · `connected <nome>` · `disconnected` | estado da ponte/conexão |

Taxas típicas: `euler`/`quat`/`accel`/`micenv` chegam na taxa configurada na bola
(10–200 Hz, padrão 50); `co2` quando muda ou 1×/s; `mics`/`pot` 5×/s;
`diag` 1×/s. O `--imu-div` decima o `micenv` junto com a orientação: eles viajam
no mesmo pacote.

### Áudio: o Pd recebe as variáveis tratadas, não as amostras

A ponte **não** encaminha o áudio cru. O que chega é o envelope já calculado na
bola — RMS e pico por canal, em dBFS (`micenv`) — porque é isso que um patch
usa como sinal de controle:

| | Amostras cruas (o que saiu) | Envelope tratado (o que fica) |
|---|---|---|
| Mensagens/s | ~21, com 96 floats cada ≈ 2000 floats/s | 1 por pacote de orientação, 4 floats |
| Pronto para usar? | não: exigiria janela, RMS e detecção de pico dentro do Pd | sim: dois números por canal, em dBFS |
| Fidelidade do pico | o stream é decimado por média de 8 — transientes sumiam | pico amostrado nos 16 kHz cheios |
| Custo no enlace BLE | ~4,3 kB/s e ~45 ms/s de tempo de ar (mais que todos os outros pacotes somados) | 8 bytes num pacote que já era enviado |

**E o espectro (FFT)?** Fica só no navegador, no config-app, que é onde ele serve
para ajustar os cortes do passa-banda vendo o resultado. Mandar espectro pelo
BLE custaria mais banda que mandar o próprio áudio (256 raias por canal × 21/s)
e não cabe no MTU. Se precisar de análise espectral dentro do Pd, o caminho é
analisar o sinal de um microfone local ali mesmo — não transportar áudio da bola.
O `mics` continua informando, no último campo, se o passa-banda do sopro estava
ativo quando aquele RMS foi medido.

## Opções da ponte

```
python3 gaiaball_bridge.py --help
```

| Opção | Padrão | Efeito |
|---|---|---|
| `--name NOME` | `Globo_1` | nome BLE da bola a procurar |
| `--address END` | — | conecta por endereço (MAC no Win/Linux, UUID no macOS); ignora `--name` |
| `--port N` | `9478` | porta UDP (tem que casar com o argumento do `[gaiaball]`) |
| `--host IP` | `127.0.0.1` | manda os dados para outro computador (Pd em outra máquina) |
| `--imu-div N` | `1` | encaminha 1 a cada N pacotes de IMU (alivia patches pesados: `--imu-div 4` a 200 Hz ≈ 50 msg/s). Vale também para o `micenv`, que viaja no mesmo pacote |
| `--list` | — | só lista os dispositivos BLE visíveis (a Gaiaball aparece marcada) e sai |
| `-v` | — | imprime contadores de pacotes a cada 5 s |

## Exemplos

O `gaiaball-help.pd` traz os três prontos:

1. **Orientação → som**: `euler` → `[unpack f f f]` → yaw (−180…180°) mapeado
   para a frequência de um `[osc~]`.
2. **Sopro → evento**: o 4º campo de `co2` (pulso virtual) → `[sel 1]` → bang.
   É o mesmo gatilho que o site usa para disparar a composição.
3. **Nível dos microfones**: `micenv` → RMS e pico por canal na taxa de orientação,
   pronto para controlar amplitude/filtros (SPL estimado ≈ dBFS + 120). O outlet
   `mics`, de 5 Hz, serve como leitura lenta/diagnóstico.

## Solução de problemas

| Sintoma | Causa provável / correção |
|---|---|
| `status` fica em `scanning` | bola desligada, fora de alcance, ou já com **2 centrais conectadas** (o limite do firmware — ex.: site + config-app). Desconecte um deles. |
| Nada chega no Pd mas a ponte diz `conectado` | porta diferente dos dois lados (`--port` × argumento do `[gaiaball]`), ou firewall bloqueando UDP local (raro em loopback). |
| macOS: a ponte aborta na hora | permissão de Bluetooth negada ao Terminal — conceda em Ajustes → Privacidade e Segurança → Bluetooth e rode de novo. |
| Linux: `org.bluez` error / adaptador não achado | `sudo systemctl start bluetooth`; confira `bluetoothctl show`. |
| Windows: a bola não aparece nem no `--list` | Bluetooth desativado ou rádio sem BLE (4.0+); teste com um dongle BLE. |
| Quero o áudio cru / o espectro no Pd | não é oferecido de propósito (ver [Áudio](#áudio-o-pd-recebe-as-variáveis-tratadas-não-as-amostras)). Use `micenv` para nível e transiente; o espectro fica no config-app. |
| `micenv` não chega | a bola está rodando firmware anterior à v8 (pacote de orientação de 23 B em vez de 31 B) — atualize o firmware; a ponte descarta pacotes curtos. |
| Ângulos "diferentes do site" | não são: é a mesma convenção (`quatToEulerLegacy`). Lembre que zero/montagem são aplicados **na bola** — use o comando "Zerar orientação" do config-app. |

## Notas de projeto

- **Somente leitura, sem PIN**: as características de notificação e a leitura
  da config são abertas por design (o PIN protege só escritas/comandos) — a
  ponte não precisa e não pede desbloqueio.
- **Duas centrais**: o firmware aceita 2 conexões simultâneas. Ponte + site,
  ou ponte + config-app — escolha dois.
- A fonte da verdade do protocolo é `firmware/docs/protocol.md` (deste repo);
  a ponte decodifica o wire protocol v1 e é independente do site e do
  config-app.
