# Gaiaball — aceleração em mg, ponte para o Pd pela config-app, detector de sopro, gráficos e modo palco

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

Junto vêm as mudanças que o uso no palco pede:

- **Aceleração em mg (mili-g), pronta para uso.** Hoje a bola manda a aceleração em
  contagens brutas do sensor (8192 = 1 g) e cada consumidor converte (ou não: o site repassa
  contagens aos patches, embora o vocabulário diga "g"). A bola passa a mandar números
  inteiros em mg (1 g = 1000 mg), a unidade das fichas técnicas de acelerômetros, e todos os
  consumidores usam o número direto.

- **Detector de sopro escolhível: absoluto ou adaptativo.** O detector atual é adaptativo
  (dispara quando o som sobe acima de uma linha de base do ruído ambiente) e por isso a
  linha de limiar desenhada nos gráficos engana: numa sala a −30 dBFS o limiar padrão
  aparece em −35,5 dBFS, mas o disparo só acontece em −26,3 dBFS. Entra um detector
  absoluto, que compara o nível medido com uma linha fixa (o que aparece no gráfico é o que
  dispara), e o adaptativo continua como opção. Nos dois, a linha do gráfico passa a mostrar
  o nível real de disparo.
- **Gráficos honestos e legíveis.** Eixo Y com a faixa certa desde o início, janela de tempo
  escolhível (5, 8 ou 12 s) e todos os pacotes desenhados, não só um a cada 100 ms.
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
- Detector de sopro **absoluto ou adaptativo**, escolhido na UI com a descrição de cada um;
  sensibilidade pelo potenciômetro físico **ou** por um valor em dBFS na UI; medida em
  **RMS ou pico**; rearme configurável em dB; linha de limiar honesta nos gráficos e um
  gráfico novo com o pulso de detecção vindo da própria bola.
- Gráficos de linha da config-app com a faixa do eixo Y certa desde o início, janela de 5, 8
  ou 12 s e todos os pacotes recebidos.
- A bola emite a aceleração em mg (inteiros de 16 bits) pelo BLE; protocolo, config-app,
  site, `[gaiaball]`, patches e documentação usam mg. `gaia.acc.*` e `gaia.sensors` chegam
  aos patches em mg.
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
- Mudanças incompatíveis no protocolo além da aceleração: só o pacote de movimento muda de
  formato (versão 2); todo o resto é aditivo dentro da versão 1.
- Mudar o fundo de escala do acelerômetro (±4 g). O `int16` em mg comporta até ±32,767 g,
  então isso passa a ser possível sem mexer no protocolo, mas não entra agora.

## Anti-objetivos

- Qualquer passo que exija terminal, Python ou instalar programa além do Pd e do Chrome.
- O Pd receber dados atrasados por causa da tela: a página repassa primeiro e desenha
  depois, e descarta pacotes velhos em vez de acumular.
- Uma linha de limiar que não corresponda ao disparo real.
- Quebrar o site GaiaSenses sem atualizá-lo junto. O pacote de movimento muda de formato, e
  o site novo precisa ser publicado na mesma ocasião em que as bolas são gravadas (ver
  "Riscos"). Fora isso, o site não assina a característica de diagnóstico e ignora os bits
  novos.

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
- **O site envia hoje `gaia.acc.*` e os três valores de aceleração de `gaia.sensors` em
  contagens brutas do acelerômetro (8192 = 1 g)**, embora `lib/gaia-vocabulary.json` declare
  "g". O `paraisoGaia43` é o único patch que usa esses canais e divide por 8000 em vários
  pontos (11 objetos `[/ 8000]` em `patches/paraisoGaia43/main.pd`) e usa o `gaia.acc.z` em
  contagens como "gravidade" (`[s gravidade_$0]`). Decisão do usuário: a bola emite mg
  (inteiros), e protocolo, site, patches e documentação passam a usar mg.
- **Euler do site:** `quatToEulerLegacy` em `Gaiasenses-web/app/[locale]/map3/ble-decode.ts`
  (troca de eixos legada: `qw=w, qx=y, qy=x, qz=-z`). O `[gaiaball]` usa exatamente a mesma
  conversão para `gaia.sensors`.
- **Bits livres:** `ImuPacket.flags` usa os bits 0–3; `SensorsPacket.flags` usa 0–3.
  TLVs livres: `0x2B` a `0x2E` (reservados são só `0x21`, `0x27`, `0x33`, `0x35`).
- **Gráficos hoje** (`config-app/src/panels/sensors-panel.ts`): um relógio de 10 Hz
  (`TICK_MS = 100`) acrescenta a cada tick só o **último** valor recebido; o histórico é de
  120 s (`WINDOW_S`) e o eixo X se ajusta a tudo o que chegou. As minilinhas de Métricas
  (`metrics-panel.ts`) guardam 60 pontos a cada 250 ms (15 s) e se ajustam sozinhas. Nada
  disso interfere no repasse ao Pd nem no site, a não ser pela ocupação da linha de execução
  do JavaScript durante o redesenho.
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

0. Parte 0: aceleração em mg (ImuPacket versão 2) no firmware, na config-app e no site.
   Vem primeiro porque todo o resto decodifica esse pacote.
1. Parte 1: detector de sopro e gráficos (firmware + config-app). Produz o bit de sopro que
   a Parte 2 publica no Pd.
2. Parte 2: ponte config-app → `[gaiaball]` (external, CI, Deken, GitHub Pages).
3. Parte 3: modo palco / modo ajuste (usa o status do Pd da Parte 2).

Um PR no `gaiaball` com as quatro partes e um PR no `Gaiasenses-web` com o decodificador do
pacote de movimento versão 2, o vocabulário, a documentação e a cópia atualizada do guia do
protocolo.

---

## Parte 0 — Aceleração em mg (ImuPacket versão 2)

### Formato

O pacote de movimento troca o **significado** dos três `int16` de aceleração: de contagens do
sensor (8192 = 1 g) para **mg** (1 g = 1000 mg). Tamanho, ordem e posição dos campos ficam
iguais: o pacote continua com **31 bytes**.

| Offset | Bytes | Tipo | Campo |
|---|---|---|---|
| 0 | 1 | u8 | `version` = **2** |
| 1 | 1 | u8 | `type` = `0x01` |
| 2 | 2 | u16 | `seq` |
| 4 | 4 | u32 | `timestampUs` |
| 8 | 8 | 4 × i16 | quaternion Q14 (`w x y z`) |
| 16 | 6 | 3 × i16 | **aceleração X, Y, Z em mg** |
| 22 | 1 | u8 | `flags` (bit 4 = sopro, da Parte 1) |
| 23 | 4 | 2 × i16 | RMS dos microfones (E, D), dBFS ×100 |
| 27 | 4 | 2 × i16 | pico dos microfones (E, D), dBFS ×100 |

- **Versão por pacote, como manda a regra 8 do guia** ("mudança incompatível = versão nova;
  os decodificadores v1 descartam esses pacotes"): mesmo sem mudar de tamanho, os bytes 16–21
  mudam de significado, e um decodificador antigo leria mg como contagens (8,192 vezes menor).
  Só o pacote de movimento passa a levar `version = 2`; CO₂, navegação, diagnóstico e os
  demais continuam na versão 1.
- **Conversão no firmware:** `lroundf(contagens × 1000 / 8192)`, só no momento de montar o
  pacote, depois dos filtros que já existem (pico e EMA); os filtros continuam trabalhando
  com as contagens.
  - **Resolução de 1 mg.** O sensor mede em passos de 0,122 mg, mas o ruído próprio do
    acelerômetro (pelas fichas técnicas dos dois chips) fica em torno de 1 a 2 mg; o erro de
    arredondar para 1 mg (≈0,3 mg) fica abaixo dele. 1 mg ≈ 0,06° de inclinação.
  - **Faixa:** ±4 g viram ±4000 mg; o `int16` comporta até ±32 767 mg.
  - Números inteiros saem limpos em qualquer linguagem (sem o problema de mostrar
    `0.12340000271…` de um `float32`).
- **Sem custo no rádio:** o tamanho não muda; MTU continua ≥ 34 e o orçamento do README do
  firmware continua valendo.
- **Golden novo** do pacote de movimento em `test_protocol.cpp`, copiado à mão para
  `protocol.md`, `protocolo-ble.md`, `packets.test.ts` e o teste do site, como hoje.

### Consumidores

- **Decodificadores aceitam só a versão 2 para o pacote de movimento** (config-app, site,
  `[gaiaball]` e os três exemplos do guia: JavaScript, Python e C). Um pacote de movimento
  versão 1 é descartado. Uma bola com firmware antigo já é reconhecida pela falta da
  característica `…0006`, e a mensagem "atualize o firmware da bola" passa a cobrir também
  este caso.
- **Ninguém converte para g:** a aceleração é publicada e mostrada em mg em toda a cadeia.
- **config-app:** decodificador com versão 2 e aceleração em mg; sai o `ACCEL_LSB_PER_G`; o
  gráfico "Aceleração" passa a "Aceleração (mg)" (faixa na Parte 1); o gerador do `?demo=1`
  emite a versão 2.
- **Site (`Gaiasenses-web`):**
  - `app/[locale]/map3/ble-decode.ts`: pacote de movimento versão 2, aceleração em mg (os
    offsets não mudam); testes com o golden novo.
  - `gaia.acc.*` e os três valores de aceleração de `gaia.sensors` chegam aos patches em mg.
    `lib/gaiaball-live.ts` (sketches p5) e o cartão de informações do mapa (`accX`/`accY`/
    `accZ` em `gaiasenses-map.tsx`) também mostram mg.
  - `lib/gaia-vocabulary.json`: `gaia.acc.*` com unidade `mg` e faixa `[-4000, 4000]` (era
    "g" e `[-16, 16]`); `gaia.sensors` com "aceX aceY aceZ em mg" na ajuda.
  - **Zona morta (`accEpsilon`):** os valores em mg são inteiros, então a menor mudança é
    1 mg, e o padrão `0.05` continua deixando passar toda mudança, exatamente como hoje com
    contagens. O padrão (`DEFAULT_TUNING` em `scripts/gen-patch-registry.mjs`) e o do
    `paraisoGaia43` ficam como estão. O máximo do controle de ajuste no registro de patches
    (`ACC_EPSILON_MAX = 100`, em `app/[locale]/map3/pd4web-patch-log.tsx`) passa a `1000`
    (1 g em mg). A documentação explica a zona morta em mg.
  - README, `docs/musico/README.md` e `vocabulario.md`: unidade mg.
- **`paraisoGaia43` passa a trabalhar em mg** (decisão do usuário). Quem faz é o Gabriel, na
  mesma edição em que já está adaptando o patch (um único editor no arquivo, sem conflito);
  o PR do site não é publicado antes disso. O que muda, rastreado no `main.pd`:
  - os 11 `[/ 8000]` aplicados à aceleração viram `[/ 1000]`. O patch usava 8000 contagens
    como "1 g"; o certo era 8192, e em mg 1 g é exatamente 1000. A diferença é de 2,4 %, o
    equivalente a inclinar a bola cerca de 1° a mais ou a menos;
  - outras constantes em contagens viram mg, com o mesmo efeito de hoje (contagens ÷ 8,192):
    `[> 6000]` e `[< -6000]` (gatilhos ligados ao `aceX`) → `[> 732]` e `[< -732]`;
    `[/ 7000]` (gravidade → `vline~` → `pow~ 8`) → `[/ 854.5]`; `[/ 11000]` (gravidade →
    `abs` → `pow 0.7`) → `[/ 1343]`; `[+ 11000]` seguido de `[/ 22000]` → `[+ 1343]` e
    `[/ 2686]`;
  - o `[1 6000(` ligado à gravidade não muda (ali 6000 é tempo, em ms);
  - o `accEpsilon` do `patch.json` pode ficar em `0.05`.
- **`[gaiaball]`** (Parte 2): publica a aceleração em mg, como chega.

### Testes da Parte 0

- Firmware: o golden novo; `sizeof(ImuPacket) == 31`; a conversão (contagens conhecidas → mg,
  incluindo ±32768 contagens → ±4000 mg, 0, e valores que arredondam para cima e para baixo).
- config-app e site: decodificação do golden; recusa do pacote de movimento versão 1 e de
  pacotes curtos; o envelope dos microfones continua nos offsets 23–30.
- Guia: os exemplos em JavaScript, Python e C rodam contra o golden novo, como na primeira
  versão do guia.

---

## Parte 1 — Detector de sopro e gráficos

### Comportamento (firmware)

- **Dois detectores, escolhidos por configuração** (TLV `0x2D`). Os dois recebem, por
  microfone, o nível da janela de 20 ms na medida escolhida (RMS ou pico). Os dois acionam o
  que já é acionado hoje: o pulso de CO₂ virtual e o flash do LED de som. Com
  `audioEnabled = 0` nenhum dispara e o estado fica 0.
- **Absoluto (padrão):** classe pura nova em `firmware/Gaiaball/src/logic/`, com testes
  nativos.
  - Avaliado a cada bloco de áudio (5 ms).
  - **Disparo:** o nível de **qualquer um** dos dois microfones passa do limiar (o mesmo que
    "o maior dos dois passa"), o detector está armado e o debounce (`debounceMs`, TLV
    `0x13`, padrão 300 ms) já passou.
  - **Rearme:** os dois microfones caem abaixo do limiar menos o rearme configurado (TLV
    `0x2E`, em dB inteiros, padrão 3 dB). Um sopro longo gera um disparo só.
- **Adaptativo:** o `PeakDetector` de hoje, com o mesmo algoritmo (envelope rápido − linha de
  base lenta, alfas `0x11`/`0x12`, avaliado a cada 3 blocos, sobre o maior dos dois
  microfones). Muda só:
  - a entrada pode ser RMS ou pico (hoje é só RMS);
  - o rearme deixa de ser o 0,7× fixo e passa a ser o mesmo parâmetro em dB (3 dB = ×0,708,
    praticamente o comportamento de hoje);
  - o limiar vem do mesmo campo em dBFS, convertido para contagens: é o quanto o som precisa
    subir acima da linha de base.
- **Estado "sopro reconhecido"** (os dois detectores): vale 1 do disparo até o rearme.
- **Nível efetivo de disparo**, publicado para o gráfico: no absoluto é o próprio limiar; no
  adaptativo é a linha de base mais o limiar, que acompanha o ruído da sala.
- **Por que a linha fica honesta:**
  - Absoluto: a curva por microfone (RMS ou pico, do ImuPacket) vem da mesma janela de 20 ms
    que o detector usa. Com pico a correspondência é exata (a janela do pacote cobre todas as
    amostras). Com RMS, o detector avalia a cada 5 ms e o pacote mostra a cada 20 ms; um
    cruzamento muito curto pode disparar sem aparecer na curva, mas aparece no gráfico de
    pulso.
  - Adaptativo: a linha sobe e desce com o ruído (5 vezes por segundo), e o detector compara o
    nível suavizado (envelope rápido), que atrasa um pouco em relação à curva. A curva pode
    cruzar a linha um instante antes do pulso. A descrição do detector na UI diz isso.

### Protocolo (aditivo, v1, nenhum tamanho muda)

- **`ImuPacket.flags` bit 4 = `breath`** ("sopro reconhecido"). É **travado até ser
  enviado**: se o estado foi 1 em qualquer momento desde o pacote anterior, o próximo
  pacote leva 1. Assim um sopro nunca se perde entre dois pacotes de 20 ms. O bit é escrito
  pela task de áudio e lido/zerado pela task do App; a passagem é atômica.
- **`SensorsPacket.flags` bit 4 = `metricPeak`** (0 = RMS, 1 = pico) e **bit 5 =
  `adaptive`** (0 = absoluto, 1 = adaptativo).
- **`SensorsPacket.audioThreshold`** passa a ser o **nível efetivo de disparo, em contagens
  da medida ativa** (RMS ou pico, em contagens de 24 bits): o limiar no absoluto; linha de
  base + limiar no adaptativo. A config-app converte para dBFS com a fórmula da medida
  indicada pelo bit 4.
- `SensorsPacket.potRaw` continua igual (pedido explícito do usuário).
- Vetores golden novos para os dois bits em `test_protocol.cpp`, copiados à mão para
  `protocol.md`, `protocolo-ble.md`, `packets.test.ts` e o teste do site, como hoje.

### Configuração (TLVs)

| Id | Nome | Tipo | Faixa | Padrão | Observação |
|---|---|---|---|---|---|
| `0x14` | potControlsThreshold | u8 | 0/1 | 1 | Inalterado: 1 = potenciômetro, 0 = valor fixo |
| `0x2B` | breathThresholdDbX100 | i16 | −9000…0 | −3550 | Limiar fixo em dBFS ×100, na medida ativa. No adaptativo, é o quanto o som sobe acima da linha de base |
| `0x2C` | breathMetric | u8 | 0 RMS / 1 pico | 0 | |
| `0x2D` | breathDetector | u8 | 0 absoluto / 1 adaptativo | 0 | |
| `0x2E` | breathRearmDb | u8 | 0…20 dB, inteiro | 3 | Quanto o som precisa cair abaixo do limiar para rearmar |
| `0x11`, `0x12` | alfa rápido, alfa lento | u16 | inalterados | 250, 995 | Usados só pelo adaptativo |
| `0x15` (TLV, não o comando de mesmo número) | limiar fixo em contagens | — | — | — | Aposentado: vira reservado, como `0x21`/`0x27`/`0x33` |

- Os padrões (absoluto, potenciômetro, RMS, −35,5 dBFS, rearme 3 dB) preservam o
  comportamento de hoje numa sala silenciosa: −35,5 dBFS RMS é o antigo
  `thresholdFixed = 100000`. Numa sala barulhenta o absoluto dispara mais fácil que o
  adaptativo; a bola pode voltar ao adaptativo pela UI.
- **O id aposentado segue a regra que já vale para os reservados:** numa escrita, id
  desconhecido rejeita a escrita inteira (tudo ou nada, como hoje); na carga da NVS, a carga
  leniente pula o id. Só a config-app escreve esse id, e a versão nova deixa de escrevê-lo.
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
  Usado só onde há **duas opções com nome**: tipo de detector, fonte da sensibilidade, medida
  e o modo palco/ajuste. Os checkboxes liga/desliga que já existem continuam como estão.
- Grupo "Detecção de sopro", no Básico:
  - **Tipo de detector:** `Absoluto ⟷ Adaptativo` (TLV `0x2D`), com a descrição do
    escolhido logo abaixo da chave:
    - Absoluto: "Dispara quando o som de qualquer microfone passa da linha. O que você vê no
      gráfico é o que dispara. Numa sala barulhenta, suba a linha."
    - Adaptativo: "Aprende o ruído da sala em cerca de 3 segundos e dispara quando o som sobe
      acima dele pelo valor da sensibilidade. Ajusta-se sozinho ao ambiente; a linha do
      gráfico sobe e desce com o ruído, e o disparo pode vir um instante depois de a curva
      cruzá-la."
  - **Fonte da sensibilidade:** `Potenciômetro ⟷ Valor fixo` (TLV `0x14`).
  - **Medida:** `RMS ⟷ Pico` (TLV `0x2C`).
  - **Sensibilidade (dBFS):** número de −90 a 0, passo 0,5 (TLV `0x2B`). Desabilitado, com
    o valor atual do potenciômetro mostrado ao lado, quando a fonte é o potenciômetro. O
    `info:` explica o sentido em cada detector (a linha, no absoluto; o quanto acima do
    ruído, no adaptativo).
  - **Rearme (dB):** de 0 a 20, de 1 em 1 dB (número inteiro; TLV `0x2E`): "quanto o som
    precisa cair abaixo da linha para um novo sopro poder disparar".
  - **Debounce (ms):** continua (TLV `0x13`).
  - **Alfa rápido / Alfa lento** (TLVs `0x11`/`0x12`): no Avançado, visíveis só com o
    detector adaptativo (o `visible?(cfg)` que já existe).
  - Sai "Limiar fixo" (contagens).
  - `info:` didático em cada campo, no padrão dos existentes.
- O teto de campos do Básico em `config-panel.test.ts` é ajustado se a contagem passar dele,
  com o motivo registrado no teste.

### config-app: gráficos (`sensors-panel.ts` e as minilinhas de `metrics-panel.ts`)

- **Linha de limiar:** **contínua** (hoje é pontilhada), no nível efetivo de disparo em
  dBFS, atualizada a cada `SensorsPacket` (5 Hz). No absoluto é horizontal (muda só com o
  potenciômetro ou o campo); no adaptativo acompanha o ruído da sala.
  - Medida RMS → desenhada nos gráficos "Mic esquerdo (dBFS)" e "Mic direito (dBFS)".
  - Medida pico → desenhada no gráfico "Pico por canal (dBFS)".
  - Some do gráfico da medida inativa.
- **Gráfico novo "Detecção de sopro"**: degrau 0/1 a partir do bit 4 do ImuPacket (50 Hz),
  logo abaixo dos gráficos de microfone, no mesmo eixo de tempo.
- **Todos os pacotes desenhados.** Hoje o tick de 10 Hz acrescenta só o último valor de cada
  intervalo, e o que chegou entre dois ticks some (um pulso de 20 ms ou um pico curto podem
  não aparecer). Passa a guardar todos os pacotes recebidos desde o tick anterior, cada um
  com o seu horário de chegada, e acrescentá-los de uma vez. O redesenho continua 10 vezes
  por segundo.
- **Janela de tempo escolhível:** seletor `5 s · 8 s · 12 s` acima dos gráficos (padrão
  12 s, lembrado no `localStorage`). O eixo X mostra sempre os últimos N segundos,
  deslizando. O histórico guardado é o da maior opção (12 s); o histórico de 120 s sai (menos
  pontos, redesenho mais leve).
- **Eixo Y com faixa fixa desde o início** (depois de um zoom, o duplo clique volta a ela):

  | Gráfico | Faixa | Motivo |
  |---|---|---|
  | Orientação | −180 a 180 ° | faixa dos ângulos de Euler |
  | Aceleração | −4000 a 4000 mg | fundo de escala do acelerômetro (±4 g) |
  | Mic esquerdo, Mic direito (RMS) | −120 a 0 dBFS | piso do protocolo até o fundo de escala |
  | Pico por canal | −120 a 0 dBFS | idem |
  | CO₂ | 0 a 5000 ppm, ou até o pico virtual configurado (TLV `0x23`) se ele for maior | faixa do sensor SC8-CO2-5K; o pulso virtual não pode sair cortado |
  | Potenciômetro | 0 a 100 % | curso do potenciômetro |
  | Detecção de sopro | 0 a 1, com uma pequena margem | degrau |
  | Minilinha "Taxa" (Métricas) | 0 a 1,2 × a taxa configurada | |
  | Minilinha "Perda" (Métricas) | 0 a 100 % | |

- **Rodapé** troca "Janela de 120 s, amostrada a 10 Hz." por: "Últimos N s, com todos os
  pacotes recebidos. Os gráficos são redesenhados 10 vezes por segundo; isso não atrasa o
  envio ao Pd nem o site."
- O cartão do potenciômetro troca "limiar em contagens RMS" pelo limiar em dBFS e a medida.
- O modo `?demo=1` gera níveis, limiar e pulsos sintéticos coerentes.

### Testes da Parte 1

- Firmware (`pio test -e native`): o detector absoluto (dispara em L, em R, rearme no valor
  configurado, debounce, um disparo por sopro longo, RMS e pico, áudio desligado); o
  adaptativo com o rearme em dB e com pico; o nível efetivo de disparo dos dois; o bit
  travado até o envio; TLVs novos e o aposentado; mapeamento do potenciômetro em dB; goldens.
- config-app (`npm test`): decodificação dos bits; o componente de seleção (teclado, ARIA);
  conversão contagens→dBFS por medida; qual gráfico recebe a linha; todos os pacotes entre
  dois ticks entram no gráfico; a janela de 5/8/12 s; as faixas do eixo Y.

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
  | `gaia.acc.x`, `.y`, `.z` | IMU | mg (inteiros) | sim (o site passa a mandar mg, Parte 0) |
  | `gaia.co2` | CO₂ | ppm | sim |
  | `gaia.sensors` | IMU + último CO₂ | lista `roll pitch yaw accX accY accZ co2`, Euler por `quatToEulerLegacy`, aceleração em mg | sim |
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
  - O detector de sopro (absoluto ou adaptativo, o que estiver configurado) roda na bola em
    qualquer modo.
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

- `firmware/docs/protocol.md` e `docs/protocolo-ble.md`: o pacote de movimento versão 2
  (aceleração em mg no mapa de bytes, exemplos em JavaScript, Python e C, a seção de
  versionamento com este primeiro caso real de versão nova), os dois bits, o novo sentido do
  `audioThreshold`, os TLVs novos e o aposentado, os goldens. A cópia
  `Gaiasenses-web/docs/gaiaball-protocolo-ble.md` continua idêntica (mesmo sha256), no PR do
  site.
- `firmware/README.md`: a aceleração em mg, o detector absoluto e a referência rápida de
  TLVs; o comentário que diz que o ±4 g é "fixado pelo protocolo" (com mg, não é mais).
- `config-app/README.md`: o envio ao Pd, o modo palco, a URL publicada.
- `pd-gaiaball/README.md`: reescrito para leigos (instalar pelo Deken, abrir a config-app,
  criar `[gaiaball]`, liberar o Chrome, tabela de canais).
- `docs/bench-validation.md`: roteiro do sopro (sala silenciosa e barulhenta, RMS e pico,
  potenciômetro e valor fixo) e do Pd (taxa e intervalo nos dois modos).
- README raiz: link para o `pd-gaiaball` novo.
- `pd-gaiaball/PUBLICAR-NO-DEKEN.md` (já escrito): conferir os nomes dos arquivos e das
  arquiteturas contra o primeiro `.dek` real gerado pelo CI.

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
- Modo palco desliga o máximo possível e mostra a UI como travada; o sopro continua
  funcionando no palco.
- Tipo de detector escolhível na UI (absoluto ou adaptativo), com a descrição de cada um.
- Rearme configurável em dB.
- Eixo Y de todos os gráficos de linha com a faixa certa desde o início, nas unidades
  tratadas; janela de tempo de 5, 8 ou 12 s.
- A bola emite a aceleração em mg, inteiros de 16 bits; protocolo, site, patches, gráficos
  da config-app e documentação usam mg.
- Rearme em dB inteiros (passo de 1 dB).

## Decisões minhas (para o usuário conferir)

1. O pulso vem da bola (bit 4 do ImuPacket, travado até ser enviado), não de um cálculo no
   navegador.
2. Absoluto avaliado a cada bloco de 5 ms; o adaptativo mantém o algoritmo, os alfas e a
   cadência de hoje (3 blocos) e ganha RMS/pico e o rearme em dB. Rearme padrão 3,0 dB
   (faixa 0 a 20 dB); debounce mantido.
3. Potenciômetro linear em dB (−60…0); campo de −90 a 0 dBFS em passos de 0,5.
4. TLVs `0x2B` a `0x2E` novos; só o `0x15` aposentado; tamanhos de pacote inalterados.
5. Padrões: absoluto, potenciômetro, RMS, −35,5 dBFS, rearme 3 dB (0 a 20 dB).
6. Chave deslizante só nas escolhas de duas opções; checkboxes liga/desliga ficam.
7. "Modo ajuste" no lugar de "modo dev".
8. No modo palco ficam ativos a sensibilidade, o medidor, o pulso e o status do Pd.
9. Repassar só movimento, CO₂ e navegação; descartar acima de 4 KB em fila.
10. "Enviar ao Pd" ligado por padrão, porta 9478, reconexão a cada 2 s.
11. O objeto aceita só a origem da config-app publicada e `localhost`; um cliente por vez.
12. Canais com o vocabulário do site + os canais novos da tabela, enviados a cada pacote.
13. `gaiaball_bridge.py`, `requirements.txt` e a abstração antiga saem.
14. Config-app publicada no GitHub Pages do repo `gaiaball`.
15. O id aposentado rejeita a escrita inteira (como os reservados de hoje); limiar fixo
    antigo em contagens não é convertido, o `0x2B` começa em −35,5 dBFS.
16. Canais a cada pacote da origem, sem zona morta (o site amostra no relógio do patch e com
    zonas mortas); CO₂ = 0 em `gaia.sensors` até o primeiro pacote.
17. Na desconexão, `gaia.breath` e `gaia.co2.trigger` vão a 0; os outros mantêm o último valor.
18. No palco, com a fonte "Potenciômetro", o limiar ao vivo não aparece.
19. PR empilhado sobre o #3, redirecionado para `main` depois do merge dele.
20. Gráficos recebem todos os pacotes entre dois ticks; histórico de 12 s no lugar de 120 s.
21. Faixas do eixo Y da tabela da Parte 1. Valores RMS acima de 0 dBFS (só uma onda quadrada
    de fundo de escala chega a +3 dBFS) ficam cortados no topo.
22. As minilinhas de Métricas também ganham faixa fixa.
23. Aceleração em mg na mesma posição e no mesmo tamanho do pacote; versão 2 só no pacote
    de movimento (regra 8 do guia), os outros continuam na 1.
24. Decodificadores aceitam só a versão 2 do pacote de movimento (sem suporte a bolas
    antigas, que já pedem atualização pela falta da `…0006`).
25. `paraisoGaia43` em mg (`[/ 8000]` → `[/ 1000]` e as outras constantes em contagens
    convertidas), feito pelo Gabriel na mesma edição; `accEpsilon` continua 0.05.
26. Zona morta da aceleração: padrão inalterado (0.05 já deixa passar toda mudança de 1 mg);
    máximo do controle de ajuste do site de 100 para 1000 mg.

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

- **Janela de publicação.** O site que está no ar não lê o pacote de movimento versão 2, e o
  site novo exige o firmware novo (característica `…0006`). Entre gravar a primeira bola e
  publicar o site novo, a bola não funciona no site do ar. Gravar todas as bolas e publicar o
  site na mesma ocasião, depois do patch do Gabriel.
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

- O site enviar `gaia.breath` e os outros canais novos aos patches, para que o vocabulário
  seja o mesmo dos dois lados.
- Bluetooth dentro do `[gaiaball]` (caminho A), quando o Pd do macOS declarar a permissão.
