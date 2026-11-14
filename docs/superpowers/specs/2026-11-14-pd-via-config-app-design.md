# Gaiaball — ponte para o Pd pela config-app, detector de sopro absoluto e modo palco

Data: 2026-11-14 · Repo: `gaiaball` · Branch: `feat/pd-via-config-app`, criada a partir de
`feat/navegacao-globo` (PR #3), porque a ponte repassa o NavPacket que só existe lá. O PR desta
branch é empilhado sobre o #3 (base `feat/navegacao-globo`) e é redirecionado para `main`
quando o #3 for mergeado.

## Intenção

Quem usa a Gaiaball no Pure Data é, na maioria, leigo: não abre terminal, não instala
Python. Hoje o `pd-gaiaball` exige as duas coisas (`gaiaball_bridge.py` + `bleak` rodando
num terminal). O objetivo é que a pessoa:

1. abra a config-app no Chrome ou no Edge e conecte a bola (como já faz para configurá-la);
2. abra o Pd e crie `[gaiaball]` no patch;
3. receba os dados da bola em tempo real, separados por canal, sem mais nada.

Junto vêm duas mudanças que o uso no palco pede:

- **Detector de sopro absoluto.** O detector atual é adaptativo (dispara quando o som sobe
  acima de uma linha de base do ruído ambiente) e por isso a linha de limiar desenhada nos
  gráficos engana: numa sala a −30 dBFS o limiar padrão aparece em −35,5 dBFS, mas o
  disparo só acontece em −26,3 dBFS. O novo detector compara o nível medido com uma linha
  fixa: o que aparece no gráfico é o que dispara.
- **Modo palco.** A config-app desenha 3D e gráficos na mesma linha de execução do
  JavaScript que recebe o Bluetooth; durante uma apresentação isso só atrasa o repasse ao Pd.
  No modo palco quase tudo é pausado e travado.

## Objetivos

- `[gaiaball]` instalável pelo Deken (Ajuda → Encontrar externals), nos três sistemas,
  sem terminal.
- A config-app repassa ao Pd, sem alterar, os pacotes binários que chegam da bola, antes de
  qualquer decodificação ou desenho.
- No Pd, cada canal chega por um nome (`[r gaia.lat]`, `[r gaia.co2]`, …), com o mesmo
  vocabulário e as mesmas unidades que o site GaiaSenses envia aos patches: um patch feito
  no Pd do computador roda sem mudança no site.
- Sensibilidade do sopro escolhida pelo potenciômetro físico **ou** por um valor em dBFS na
  UI, medida em **RMS ou pico**, com a linha de limiar honesta nos gráficos e um gráfico
  novo com o pulso de detecção vindo da própria bola.
- Modo palco e modo ajuste, com a diferença explicada na tela.

## Não objetivos

- Bluetooth dentro do `[gaiaball]` (o "caminho A"). O desenho deixa a porta aberta: o
  decodificador fica separado do transporte, mas nada de BLE entra agora.
- Mais de uma bola por instância do Pd. Os nomes `gaia.*` são globais; duas bolas no mesmo
  Pd ficam para depois.
- O objeto servir a própria config-app (variante "localhost"). A config-app é publicada no
  GitHub Pages.
- Enviar `gaia.breath` e os outros canais novos a partir do site GaiaSenses (fica como
  follow-up, ver "Follow-ups").
- Trocar o formato de qualquer pacote BLE ou subir a versão do protocolo. Tudo aqui é
  aditivo dentro do protocolo v1.

## Anti-objetivos

- Qualquer passo que exija terminal, Python ou instalar programa além do Pd e do Chrome.
- O Pd receber dados atrasados por causa da tela: a página repassa primeiro e desenha
  depois, e descarta pacotes velhos em vez de acumular.
- Uma linha de limiar que não corresponda ao disparo real.
- Quebrar o site GaiaSenses: ele não assina a característica de diagnóstico e não lê os
  bits novos; nada do que ele lê muda de lugar ou de significado.

## Restrições (fatos verificados)

- **O navegador não fala UDP nem TCP puro.** Uma página só abre canal de tempo real com
  outro programa do computador por WebSocket. O Pd vanilla não faz o aperto de mão
  (handshake) do WebSocket, então o `[gaiaball]` precisa ser um external compilado.
- **Web Bluetooth:** só Chrome e Edge (Safari e Firefox não têm). No Linux o Chrome ainda
  pede ligar a opção em `chrome://flags`. Exige contexto seguro (HTTPS ou `localhost`).
- **Local Network Access (Chrome 147+):** um site público que abre WebSocket para
  `127.0.0.1` dispara um pedido de permissão ("Apps neste dispositivo"). Um clique, lembrado
  por site. Recusado, o envio fica bloqueado até a pessoa mudar nas configurações do site.
- **A config-app não está publicada.** Hoje só roda com `npm run dev`. O repo é público
  (`origin` = `github.com/GaiaSenses/gaiaball`) e o GitHub Pages não está ligado. Não há
  `vite.config` (base `/`). Publicada, a URL é `https://gaiasenses.github.io/gaiaball/` e a
  origem que o navegador declara é `https://gaiasenses.github.io`.
- **Firmware:** aceita 2 conexões BLE ao mesmo tempo (site + config-app funcionam juntos).
  Diagnóstico, Sensores e AudioChunk (característica `…0005`) só são enviados se alguém
  assinar essa característica (`App.cpp:307`, `:325`, `:348`).
- **O site envia `gaia.acc.*` e os três últimos valores de `gaia.sensors` em contagens
  brutas do acelerômetro (8192 = 1 g)**, embora `lib/gaia-vocabulary.json` diga "g". O
  `paraisoGaia43` depende disso (divide por 8000). O `[gaiaball]` segue o que o site
  realmente envia; corrigir o vocabulário é follow-up.
- **Euler do site:** `quatToEulerLegacy` em `Gaiasenses-web/app/[locale]/map3/ble-decode.ts`
  (troca de eixos legada: `qw=w, qx=y, qy=x, qz=-z`). O `[gaiaball]` usa exatamente a mesma
  conversão para `gaia.sensors`.
- **Bits livres:** `ImuPacket.flags` usa os bits 0–3; `SensorsPacket.flags` usa 0–3.
  TLVs livres: `0x2B` e `0x2C` (reservados são só `0x21`, `0x27`, `0x33`, `0x35`).
- **Áudio:** blocos I2S de 80 quadros a 16 kHz (5 ms); janela de 20 ms (4 blocos) com RMS e
  pico por canal (`AudioLevelWindow`); o envelope em dBFS ×100 já viaja no ImuPacket.
  RMS em dBFS segue AES17 (seno de fundo de escala = 0 dBFS); pico em dBFS tem 0 dBFS = uma
  amostra de fundo de escala.
- **Não há Mac na bancada de desenvolvimento.** O teste no macOS depende de alguém da equipe.

## Abordagens consideradas

| | Como funciona | Por que não / por que sim |
|---|---|---|
| A. `[gaiaball]` com Bluetooth próprio (SimpleBLE), via Deken | O objeto conecta direto na bola | No macOS o Pd.app não declara Bluetooth no `Info.plist` (verificado no código do Pd: só câmera e microfone), e o sistema atribui o uso ao Pd.app; sem um PR aceito no Pd ou um app auxiliar, não funciona no Mac. Licença BUSL-1.1 da SimpleBLE (só não comercial). |
| B. OSC por Wi-Fi + abstração vanilla | A bola entra no Wi-Fi e manda OSC; `[netreceive -u -b]` + `[oscparse]` | O firmware desliga o Wi-Fi de propósito (`App.cpp:52`): o rádio é um só e a convivência quebra o ritmo do BLE que o site usa. Mais bateria, mais flash, e configurar rede é o pior passo para leigos. |
| MIDI pela Web MIDI | A página manda MIDI a uma porta virtual; `[ctlin]` no Pd | 7 bits por mensagem (14 bits em duas mensagens não atômicas); porta virtual exige IAC no Mac e loopMIDI no Windows 10 (a porta embutida do Windows 11 só chega entre nov/2026 e jan/2027); a pessoa ainda escolhe a porta nas preferências do Pd. |
| **D. config-app repassa por WebSocket a um `[gaiaball]` compilado (escolhida)** | A config-app (no Chrome) conecta a bola e repassa os pacotes a `ws://127.0.0.1:9478`; o objeto decodifica e publica os canais | Funciona hoje nos três sistemas, Mac incluído (quem usa Bluetooth é o Chrome, que já tem a permissão). O external é só rede local: sem licença extra, mesmo código nos três sistemas. Custo: aba do Chrome aberta durante o uso. |

O usuário escolheu D. O decodificador do `[gaiaball]` fica separado do transporte para que A
possa ser acrescentado depois sem mudar nenhum patch.

## Ordem de entrega

1. Parte 1: detector absoluto e UI do sopro (firmware + config-app). Independente das
   outras; produz o bit de sopro que a Parte 2 publica no Pd.
2. Parte 2: ponte config-app → `[gaiaball]` (external, CI, Deken, GitHub Pages).
3. Parte 3: modo palco / modo ajuste (usa o status do Pd da Parte 2).

Um PR no `gaiaball` com as três partes e um PR pequeno no `Gaiasenses-web` só com a cópia
atualizada do guia do protocolo.

---

## Parte 1 — Detector de sopro absoluto

### Comportamento (firmware)

- Substitui o `PeakDetector` (adaptativo: envelope rápido − linha de base lenta) por um
  detector absoluto, numa classe pura nova em `firmware/Gaiaball/src/logic/` com testes
  nativos. O `PeakDetector` e seus testes saem.
- Entrada a cada bloco de áudio (5 ms; o `kDetectorBlocks = 3` sai): o nível da janela de
  20 ms de **cada microfone separadamente**, na medida escolhida (RMS ou pico), e o limiar.
- **Disparo:** quando o nível de **qualquer um** dos dois microfones passa do limiar (é o
  mesmo que "o maior dos dois passa do limiar"), o detector está armado e o debounce
  (`debounceMs`, TLV `0x13`, padrão 300 ms) já passou.
- **Rearme:** quando os dois microfones caem **3 dB abaixo** do limiar (×0,708 em amplitude;
  equivale ao 0,7× de hoje). Um sopro longo gera um disparo só.
- **Estado "sopro reconhecido":** vale 1 do disparo até o rearme.
- O disparo continua acionando o que já aciona hoje: o pulso de CO₂ virtual e o flash do
  LED de som.
- Com `audioEnabled = 0` nada dispara e o estado fica 0.
- **Por que funciona no gráfico:** a curva por microfone que a config-app desenha (RMS ou
  pico, do ImuPacket) vem da mesma janela de 20 ms que o detector usa. Para a medida de pico
  a correspondência é exata (a janela do pacote cobre todas as amostras). Para RMS, o
  detector avalia a cada 5 ms e o pacote mostra a cada 20 ms; um cruzamento muito curto pode
  disparar sem aparecer na curva, mas aparece no gráfico de pulso (ver "Bit de sopro").

### Protocolo (aditivo, v1, nenhum tamanho muda)

- **`ImuPacket.flags` bit 4 = `breath`** ("sopro reconhecido"). É **travado até ser
  enviado**: se o estado foi 1 em qualquer momento desde o pacote anterior, o próximo
  pacote leva 1. Assim um sopro nunca se perde entre dois pacotes de 20 ms. O bit é escrito
  pela task de áudio e lido/zerado pela task do App; a passagem é atômica.
- **`SensorsPacket.flags` bit 4 = `metricPeak`** (0 = RMS, 1 = pico).
- **`SensorsPacket.audioThreshold`** passa a ser o limiar efetivo **em contagens da medida
  ativa** (RMS em contagens de 24 bits ou pico em contagens de 24 bits). Já é o limiar
  efetivo hoje (muda com o potenciômetro); só a medida passa a variar. A config-app converte
  para dBFS com a fórmula da medida indicada pelo bit 4.
- `SensorsPacket.potRaw` continua igual (pedido explícito do usuário).
- Vetores golden novos para os dois bits em `test_protocol.cpp`, copiados à mão para
  `protocol.md`, `protocolo-ble.md`, `packets.test.ts` e o teste do site, como hoje.

### Configuração (TLVs)

| Id | Nome | Tipo | Faixa | Padrão | Observação |
|---|---|---|---|---|---|
| `0x14` | potControlsThreshold | u8 | 0/1 | 1 | Inalterado: 1 = potenciômetro, 0 = valor fixo |
| `0x2B` | breathThresholdDbX100 | i16 | −9000…0 | −3550 | Limiar fixo em dBFS ×100, na medida ativa |
| `0x2C` | breathMetric | u8 | 0 RMS / 1 pico | 0 | |
| `0x11`, `0x12`, `0x15` (TLVs, não os comandos de mesmo número) | alfa rápido, alfa lento, limiar fixo em contagens | — | — | — | Aposentados: viram reservados, como `0x21`/`0x27`/`0x33` |

- Os padrões (potenciômetro, RMS, −35,5 dBFS) preservam o comportamento de hoje numa sala
  silenciosa: −35,5 dBFS RMS é o antigo `thresholdFixed = 100000`.
- **Ids aposentados seguem a regra que já vale para os reservados:** numa escrita, id
  desconhecido rejeita a escrita inteira (tudo ou nada, como hoje); na carga da NVS, a carga
  leniente pula o id. Só a config-app escreve esses ids, e a versão nova deixa de escrevê-los.
- **Bolas já gravadas:** um limiar fixo em contagens salvo na NVS **não é convertido**. Ele
  media o excesso sobre a linha de base do detector adaptativo, outra grandeza; converter
  daria um número sem sentido. O `0x2B` começa no padrão (−35,5 dBFS) e a fonte (`0x14`)
  salva é preservada.
- `GaiaConfig`, `TlvCodec` (serializar, validar, aplicar, carga leniente),
  `applySnapshot` e `FIELD_DEFAULTS` da config-app com paridade.

### Potenciômetro

- Mapeamento **linear em dB**: ADC 0…4095 → −60…0 dBFS na medida ativa. Hoje é linear em
  contagens (10 000…500 000), o que espreme quase toda a faixa útil no começo do curso.
- O suavizador EMA (`potEmaAlphaX1000`, TLV `0x1B`) continua.
- A saída só muda quando o novo valor difere do atual em 0,5 dB ou mais.

### config-app: controles

- **Componente novo de seleção deslizante** (pedido do usuário): uma chave que desliza para
  a esquerda ou para a direita, com o nome das duas opções escrito dos dois lados. Acessível:
  `role="switch"` com `aria-checked`, rótulo associado, Espaço e setas alternam, foco
  visível, tokens de tema existentes. Novo `kind` em `Field` do `config-panel.ts`.
  Usado só onde há **duas opções com nome**: fonte da sensibilidade, medida e o modo
  palco/ajuste. Os checkboxes liga/desliga que já existem continuam como estão.
- Grupo "Detecção de sopro", no Básico:
  - **Fonte da sensibilidade:** `Potenciômetro ⟷ Valor fixo` (TLV `0x14`).
  - **Medida:** `RMS ⟷ Pico` (TLV `0x2C`).
  - **Sensibilidade (dBFS):** número de −90 a 0, passo 0,5 (TLV `0x2B`). Desabilitado, com
    o valor atual do potenciômetro mostrado ao lado, quando a fonte é o potenciômetro.
  - **Debounce (ms):** continua (TLV `0x13`).
  - Saem "Alfa rápido", "Alfa lento" e "Limiar fixo" (contagens).
  - `info:` didático em cada campo, no padrão dos existentes.
- O teto de campos do Básico em `config-panel.test.ts` é ajustado se a contagem passar dele,
  com o motivo registrado no teste.

### config-app: gráficos (`sensors-panel.ts`)

- **Linha de limiar:** horizontal, **contínua** (hoje é pontilhada), no valor efetivo em
  dBFS, atualizada a cada `SensorsPacket` (5 Hz; acompanha o potenciômetro).
  - Medida RMS → desenhada nos gráficos "Mic esquerdo (dBFS)" e "Mic direito (dBFS)".
  - Medida pico → desenhada no gráfico "Pico por canal (dBFS)".
  - Some do gráfico da medida inativa.
- **Gráfico novo "Detecção de sopro"**: degrau 0/1 a partir do bit 4 do ImuPacket (50 Hz),
  logo abaixo dos gráficos de microfone, no mesmo eixo de tempo.
- O cartão do potenciômetro troca "limiar em contagens RMS" pelo limiar em dBFS e a medida.
- O modo `?demo=1` gera níveis, limiar e pulsos sintéticos coerentes.

### Testes da Parte 1

- Firmware (`pio test -e native`): o detector novo (dispara em L, em R, rearme a −3 dB,
  debounce, um disparo por sopro longo, RMS e pico, áudio desligado); o bit travado até o
  envio; TLVs novos e aposentados; mapeamento do potenciômetro em dB; goldens.
- config-app (`npm test`): decodificação dos bits; o componente de seleção (teclado, ARIA);
  conversão contagens→dBFS por medida; qual gráfico recebe a linha.

---

## Parte 2 — Ponte config-app → `[gaiaball]`

### Fluxo

```
Gaiaball ──BLE──▶ config-app (Chrome/Edge, GitHub Pages)
                    │ 1º: ws.send(bytes do pacote, sem alterar)   ← antes de tudo
                    │ 2º: decodifica e desenha
                    ▼
            ws://127.0.0.1:9478 ──▶ [gaiaball] (external no Pd) ──▶ [r gaia.*]
```

### config-app: cliente WebSocket

- Repassa ao Pd **só** os pacotes de movimento (`0x01`), CO₂ (`0x02`) e navegação (`0x07`).
  Diagnóstico, Sensores, AudioChunk e MagChunk ficam só na tela.
- O repasse é a **primeira** coisa no tratamento de `characteristicvaluechanged`
  (`ble/gaiaball-client.ts:181`), antes da decodificação e do `dispatchEvent`. Os bytes vão
  num quadro binário de WebSocket, um pacote por mensagem (o byte de tipo já diz o canal).
- **Pacote velho é descartado:** se `ws.bufferedAmount` passar de 4 KB, o pacote não é
  enviado (contado como descartado no status).
- **Liga sozinho:** chave "Enviar ao Pd" ligada por padrão; tenta conectar ao conectar a
  bola e de 2 em 2 s enquanto o Pd não responde, sem mensagens de erro repetidas.
- **Status visível** no cabeçalho: "Pd: aguardando" / "Pd: conectado · N pacotes/s" /
  "Pd: bloqueado pelo Chrome" (com o passo para liberar em Configurações do site).
- **Porta:** 9478 por padrão, editável no Avançado, guardada em `localStorage` (conveniência
  por navegador).
- O modo `?demo=1` também repassa os pacotes sintéticos: permite testar o Pd sem bola.

### Objeto `[gaiaball]`

- External em C (C99), `pd-lib-builder`, em `pd-gaiaball/`. Substitui a abstração atual,
  `gaiaball_bridge.py` e `requirements.txt`, que saem.
- **Servidor WebSocket mínimo** (RFC 6455) escutando **só em `127.0.0.1`**, na porta do
  argumento (`[gaiaball]` = 9478, `[gaiaball 9500]` = 9500).
  - Escutar só em loopback evita o aviso do firewall do Windows e não é "rede local" para o
    macOS.
  - Sockets integrados ao laço do Pd por `sys_addpollfn` (sem threads), não bloqueantes.
  - Aperto de mão com SHA-1 + base64 embutidos (implementações pequenas de domínio
    público); quadros com máscara, `ping`/`pong`, `close`; mensagens fragmentadas e de texto
    são recusadas.
  - Aceita só conexões cujo cabeçalho `Origin` seja o da config-app publicada
    (`https://gaiasenses.github.io`) ou `http://localhost`/`http://127.0.0.1` em qualquer
    porta (desenvolvimento). Outros sites são recusados.
  - Um cliente por vez: uma conexão nova substitui a anterior.
  - Mais de um `[gaiaball]` na mesma porta: o segundo avisa no console do Pd e fica inerte.
- **Decodificador separado do transporte** (arquivo próprio), em C, espelhando o
  `PacketCodec` do firmware: confere versão e tipo, aceita tamanho ≥ o mínimo (campos
  acrescentados no futuro são ignorados), nunca trava com pacote malformado.
- **Canais publicados** (a cada pacote, sem zona morta; quem quiser filtra com `[change]`):

  | Nome | Origem | Valor | Igual ao site? |
  |---|---|---|---|
  | `gaia.lat`, `gaia.lon` | Nav (`valid`=1) | graus | sim |
  | `gaia.speed` | Nav | °/s | sim |
  | `gaia.acc.x`, `.y`, `.z` | IMU | contagens brutas (8192 = 1 g), como o site | sim |
  | `gaia.co2` | CO₂ | ppm | sim |
  | `gaia.sensors` | IMU + último CO₂ | lista `roll pitch yaw accX accY accZ co2`, Euler por `quatToEulerLegacy` | sim |
  | `gaia.bearing` | Nav | graus (convenção do Mapbox) | novo |
  | `gaia.settled` | Nav bit 1 | 0/1 | novo |
  | `gaia.quat` | IMU | lista `w x y z` | novo |
  | `gaia.mic.rms`, `gaia.mic.peak` | IMU (bit 3 = áudio válido) | lista `L R` em dBFS | novo |
  | `gaia.breath` | IMU bit 4 | 0/1 | novo |
  | `gaia.co2.trigger` | CO₂ bit 3 | 0/1 | novo |

  Os nomes novos seguem a convenção do vocabulário do site (ids em inglês).
- **Ritmo:** cada canal sai a cada pacote da sua origem: os do IMU a 50 Hz (incluindo
  `gaia.sensors`, que leva o último CO₂ recebido), os de navegação a 50 Hz, os de CO₂ a cada
  pacote de CO₂. O site, por sua vez, amostra no próprio relógio do patch (`pollMs`) e aplica
  zonas mortas (`epsilon`, `accEpsilon`). Um patch que só lê os valores roda igual nos dois;
  um patch que dispara um evento por mensagem recebe mais mensagens no computador.
- **Antes dos dados e com dados inválidos:**
  - `gaia.sensors` usa CO₂ = 0 até chegar o primeiro pacote de CO₂ (igual ao site).
  - Com Nav `valid`=0 (navegação desligada), lat/lon/speed/bearing/settled não são enviados.
  - `gaia.mic.*` só sai com o bit 3 do IMU (áudio válido) ligado; o valor vai como chega
    (o piso do protocolo é −120 dBFS).
- **Desconexão** (WebSocket fechado ou 2 s sem pacotes; o IMU manda 50 por segundo, então 2 s
  de silêncio é desconexão): o status manda `connected 0` e os canais 0/1 `gaia.breath` e
  `gaia.co2.trigger` recebem 0 uma vez, para nenhum 1 ficar preso no patch. Os demais
  canais mantêm o último valor.
- **Um outlet de status**: `connected 0|1` na mudança, e uma vez por segundo
  `rate <pacotes/s>` e `gap <maior intervalo entre pacotes, ms>`. É o que permite medir a
  diferença entre o modo palco e o modo ajuste.
- **Patch de ajuda** `gaiaball-help.pd`: todos os `[r gaia.*]` com faixa e unidade; um
  botão que abre a config-app no navegador (`[pdcontrol]` → `browse`); o passo a passo de
  uso em português.
- Pd vanilla ≥ 0.51, precisão simples e dupla (Pd64).

### Empacotamento e distribuição

- **CI no GitHub Actions** compila o external para Windows (amd64), Linux (amd64 e arm64)
  e macOS (binário único Intel + Apple Silicon), nas duas precisões do Pd, roda os testes e
  gera o pacote `.dek` como artefato. `pd-lib-builder` entra como submódulo; os cabeçalhos
  do Pd vêm do código-fonte do Pd baixado no CI; o nome das arquiteturas no `.dek` é o que o
  `deken package` gera a partir dos binários. A organização dos jobs fica com o construtor.
- **Publicar no Deken** (`deken upload`) é passo manual do Nycholas: exige conta no
  puredata.info.

### Publicação da config-app

- Workflow do GitHub Actions publica `config-app/dist` no GitHub Pages do repo:
  `https://gaiasenses.github.io/gaiaball/`. `vite.config` com `base: '/gaiaball/'`.
- Ligar o Pages (Settings → Pages → Source: GitHub Actions) é passo manual do Nycholas.
- `npm run dev` continua funcionando para desenvolvimento.

### Testes da Parte 2

- C, rodando no CI e localmente: o decodificador contra os goldens do firmware (incluindo os
  bits novos da Parte 1); o aperto de mão contra o vetor da RFC 6455
  (`dGhlIHNhbXBsZSBub25jZQ==` → `s3pPLMBiTxaQ9kYGzzo2pMbHEm0=`); quadros com máscara,
  fragmentados, de texto, `close`; `Origin` aceito e recusado.
- Ponta a ponta sem bola: Pd vanilla no Linux com um patch de teste + a config-app em
  `?demo=1` (Playwright) repassando pacotes sintéticos; conferir que os `[r gaia.*]` recebem
  os valores esperados.
- config-app (`npm test`): o filtro de tipos repassados, o descarte por `bufferedAmount`, a
  reconexão.
- Bancada (usuário): Windows com a bola real; macOS com alguém da equipe.

---

## Parte 3 — Modo palco / modo ajuste

- **Chave no cabeçalho**, com o componente de seleção da Parte 1: `Modo ajuste ⟷ Modo palco`.
  O nome "modo ajuste" é proposta no lugar de "modo dev" (quem não programa pode não
  entender "dev"); trocar o rótulo é uma linha.
- **Texto na tela**, junto da chave:
  - Modo palco: "Para apresentar. Os dados vão para o Pd com o menor atraso possível. O 3D,
    os gráficos e os ajustes ficam pausados."
  - Modo ajuste: "Para preparar e ajustar. Mostra tudo. O envio ao Pd continua, mas pode
    atrasar alguns milissegundos quando a tela está ocupada."
- **No modo palco, desliga:** o laço de desenho do 3D (`orientation-scene.ts:172`), as
  atualizações dos gráficos e das métricas (os `setInterval` de `main.ts`, `metrics-panel.ts`
  e `sensors-panel.ts`), o espectro, e a assinatura da característica `…0005`
  (`stopNotifications`; diagnóstico, Sensores e AudioChunk param de ser enviados pela bola).
  A decodificação para a tela também para; só o repasse ao Pd e a barra do palco seguem.
- **Visual travado:** todos os painéis ficam com `inert` (sem clique, sem Tab, fora da
  leitura de tela), esmaecidos (opacidade reduzida, tons de cinza) e com um cadeado e a
  legenda "Pausado no modo palco".
- **Continua ativo, numa barra própria:**
  - status do Pd (conectado, pacotes/s);
  - **sensibilidade do sopro**: o valor em dBFS ajustável quando a fonte é "Valor fixo"
    (com a fonte "Potenciômetro", só a indicação "controlado pelo potenciômetro");
  - medidor leve do nível dos microfones e o pulso de sopro, a partir do ImuPacket que já
    chega, redesenhado em no máximo 10 Hz;
  - Desconectar e "Sair do modo palco".
  - O detector de sopro roda na bola, absoluto, em qualquer modo.
- **Conexões no modo palco:** só a assinatura de `…0005` é desfeita. A característica de
  configuração continua conectada e gravável (é por ela que a sensibilidade em dBFS é
  escrita); movimento, CO₂ e navegação continuam assinados.
- **Com a fonte "Potenciômetro" o limiar ao vivo não aparece no palco**, de propósito: ele só
  chega no `SensorsPacket` (5 Hz, em `…0005`). O medidor e o pulso continuam mostrando se o
  sopro está disparando.
- **Ao sair** do modo palco: reassina `…0005` e retoma tudo como estava.
- **Padrão:** modo ajuste. A escolha é lembrada no `localStorage`.
- `?demo=1` também entra e sai do modo palco.
- **Testes:** `npm test` para a lógica de entrar e sair (o que é pausado e retomado, a
  assinatura `…0005`); Playwright com `?demo=1` para o visual travado, o Tab que não entra
  nos painéis e o repasse ao Pd continuando.

---

## Documentação a atualizar

- `firmware/docs/protocol.md` e `docs/protocolo-ble.md`: os dois bits, o novo sentido do
  `audioThreshold`, os TLVs novos e aposentados, os goldens. A cópia
  `Gaiasenses-web/docs/gaiaball-protocolo-ble.md` continua idêntica (mesmo sha256), no PR do
  site.
- `firmware/README.md`: o detector absoluto e a referência rápida de TLVs.
- `config-app/README.md`: o envio ao Pd, o modo palco, a URL publicada.
- `pd-gaiaball/README.md`: reescrito para leigos (instalar pelo Deken, abrir a config-app,
  criar `[gaiaball]`, liberar o Chrome, tabela de canais).
- `docs/bench-validation.md`: roteiro do sopro (sala silenciosa e barulhenta, RMS e pico,
  potenciômetro e valor fixo) e do Pd (taxa e intervalo nos dois modos).
- README raiz: link para o `pd-gaiaball` novo.

## Decisões do usuário

- Caminho D (config-app + `[gaiaball]` por WebSocket); repassar ao Pd antes de desenhar.
- Diagnóstico mantido no protocolo; no lugar da remoção, o modo palco.
- Detector absoluto, com RMS ou pico escolhido na UI.
- Os dois microfones individuais, não somados.
- Fonte da sensibilidade: potenciômetro físico ou valor em dBFS (≤ 0) na UI; o potenciômetro
  continua no protocolo.
- Linha horizontal contínua no gráfico da medida ativa, mudando dinamicamente; gráfico novo
  com o pulso 0→1→0.
- Seleções de duas opções como chave deslizante com as duas descrições, não checkbox.
- Modo palco desliga o máximo possível e mostra a UI como travada; o sopro continua com o
  detector absoluto.

## Decisões minhas (para o usuário conferir)

1. O pulso vem da bola (bit 4 do ImuPacket, travado até ser enviado), não de um cálculo no
   navegador.
2. Rearme a 3 dB abaixo do limiar e debounce mantido; detector avaliado a cada bloco de 5 ms.
3. Potenciômetro linear em dB (−60…0); campo de −90 a 0 dBFS em passos de 0,5.
4. TLVs `0x2B`/`0x2C` novos; `0x11`, `0x12`, `0x15` aposentados; tamanhos de pacote
   inalterados.
5. Padrões que preservam o comportamento de hoje: potenciômetro, RMS, −35,5 dBFS.
6. Chave deslizante só nas escolhas de duas opções; checkboxes liga/desliga ficam.
7. "Modo ajuste" no lugar de "modo dev".
8. No modo palco ficam ativos a sensibilidade, o medidor, o pulso e o status do Pd.
9. Repassar só movimento, CO₂ e navegação; descartar acima de 4 KB em fila.
10. "Enviar ao Pd" ligado por padrão, porta 9478, reconexão a cada 2 s.
11. O objeto aceita só a origem da config-app publicada e `localhost`; um cliente por vez.
12. Canais com o vocabulário do site (incluindo a aceleração em contagens brutas) + os
    canais novos da tabela, enviados a cada pacote.
13. `gaiaball_bridge.py`, `requirements.txt` e a abstração antiga saem.
14. Config-app publicada no GitHub Pages do repo `gaiaball`.
15. Ids aposentados rejeitam a escrita inteira (como os reservados de hoje); limiar fixo
    antigo em contagens não é convertido, o `0x2B` começa em −35,5 dBFS.
16. Canais a cada pacote da origem, sem zona morta (o site amostra no relógio do patch e com
    zonas mortas); CO₂ = 0 em `gaia.sensors` até o primeiro pacote.
17. Na desconexão, `gaia.breath` e `gaia.co2.trigger` vão a 0; os outros mantêm o último valor.
18. No palco, com a fonte "Potenciômetro", o limiar ao vivo não aparece.
19. PR empilhado sobre o #3, redirecionado para `main` depois do merge dele.

## Deixado ao construtor

- Nomes de arquivos, classes e funções; a organização interna do external e dos módulos da
  config-app.
- As implementações de SHA-1 e base64 (pequenas, de domínio público ou equivalentes).
- O desenho visual da chave deslizante e da barra do palco, dentro dos tokens de tema.
- A organização dos jobs do CI.

## Passos manuais do Nycholas

- Ligar o GitHub Pages do repo (Source: GitHub Actions).
- Publicar o pacote no Deken (`deken upload`, conta no puredata.info).
- Gravar o firmware novo nas bolas e rodar o roteiro de bancada (sopro, Pd no Windows).
- Conseguir alguém com Mac para o teste no macOS.

## Riscos

- **Muda quando as composições abrem no site.** O sopro aciona o CO₂ virtual, que aciona a
  composição. Sem a linha de base adaptativa, numa sala barulhenta o limiar precisa ser
  subido à mão (é para isso que existem o potenciômetro, o valor fixo e o gráfico). Testar
  em bancada antes de publicar.
- **Aba do Chrome em segundo plano.** Os pacotes chegam por evento, não por temporizador,
  então em princípio não sofrem o estrangulamento de abas escondidas. Precisa ser medido
  (status `rate`/`gap` do objeto) com a janela do Chrome atrás do Pd.
- **Permissão do Chrome recusada.** Sem ela, o envio fica bloqueado. A config-app explica
  como liberar.
- **Linux:** o Web Bluetooth do Chrome exige ligar uma opção em `chrome://flags`. A
  config-app avisa quando o navegador não oferece Web Bluetooth.
- **macOS sem teste local:** o external não usa Bluetooth nem rede local, então o risco é
  baixo, mas não há como testar aqui.

## Follow-ups

- Corrigir em `Gaiasenses-web/lib/gaia-vocabulary.json` a unidade de `gaia.acc.*` (o fio
  leva contagens brutas, não g).
- O site enviar `gaia.breath` e os outros canais novos aos patches, para que o vocabulário
  seja o mesmo dos dois lados.
- Bluetooth dentro do `[gaiaball]` (caminho A), quando o Pd do macOS declarar a permissão.
