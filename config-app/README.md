# Gaiaball Config

App web (Vite + TypeScript) para configurar e testar o Gaiaball v2 via Web Bluetooth: visualização 3D da orientação (o quaternion tratado, exatamente como chega por BLE), métricas do fluxo (taxa, perda, jitter), parâmetros do firmware, presets e um painel de **sensores em tempo real** com gráficos de linha (Plotly.js). Ele também repassa os dados da bola ao Pure Data (veja [Enviar ao Pd](#enviar-ao-pd)).

Endereço publicado, provisório: <https://gaiasenses.github.io/gaiaball/>. O workflow `config-app-pages` publica o site sozinho a cada push na `main` que mexe em `config-app/` (testes, build com a base `/gaiaball/` e deploy no GitHub Pages); também dá para rodar à mão em Actions → config-app-pages → Run workflow. Quando o domínio final existir, o endereço troca.

Interface: **tema claro/escuro** (botão no topo; sem escolha salva, segue o tema do sistema), **tooltips didáticos ⓘ** em cada parâmetro — passe o mouse, foque com Tab ou toque para ver o que o parâmetro faz e sua unidade (em telas estreitas o texto aparece como folha no rodapé) — e **layout responsivo** (desktop com o 3D fixo ao rolar, tablet e celular em uma coluna). Fontes IBM Plex embutidas via `@fontsource`: o app continua funcionando offline na bancada.

No canto superior direito do 3D, o botão de cubo (**Vistas da câmera**) leva a câmera às vistas Frente, Trás, Direita, Esquerda, Superior, Inferior e ISO, como num programa de CAD; a vista inicial continua a mesma.

## Enviar ao Pd

O Pure Data não tem Bluetooth; quem fala com a bola é a config-app, no Chrome ou no Edge, e ela
repassa os pacotes ao external `[gaiaball]` por um **WebSocket local** (`127.0.0.1`, porta
**9478** por padrão; a porta se muda no painel Conexão, bloco Pd, e precisa ser a mesma do
`[gaiaball 9500]`, por exemplo).

- A chave **Enviar ao Pd** (painel Conexão) já vem ligada. Com a bola conectada, o app procura
  o Pd sozinho a cada 2 s. Para enviar não é preciso PIN.
- Seguem ao Pd só os pacotes de movimento (`0x01`), CO₂ (`0x02`) e navegação (`0x07`). Se o Pd
  não acompanha, o app descarta o excesso em vez de acumular atraso.
- **Permissão do Chrome (uma vez):** na primeira conexão o Chrome pergunta se o site pode
  acessar **Apps neste dispositivo**; clique em **Permitir**. Sem ela, o indicador no alto da
  tela mostra "Pd: bloqueado pelo Chrome". Para corrigir: ícone à esquerda do endereço →
  Configurações do site → Apps neste dispositivo → Permitir, e recarregue.
- **Uma aba por vez:** mantenha só uma aba da config-app aberta com o envio ao Pd ligado.
- Com `?demo=1` o app gera dados sintéticos e os envia ao Pd pelo mesmo caminho dos pacotes
  reais. O `[gaiaball]` e os canais `gaia.*` estão descritos em
  [`pd-gaiaball/README.md`](../pd-gaiaball/README.md).
- **Para músicos:** o passo a passo completo, da instalação ao primeiro som (incluindo ligar
  o IMU de uma bola de fábrica), com cada canal explicado, receitas e seis patches de
  exemplo, está em [`pd-gaiaball/TUTORIAL.md`](../pd-gaiaball/TUTORIAL.md).

## Modo ajuste e modo palco

A chave **Modo ajuste ⟷ Modo palco** fica no topo.

- **Modo ajuste:** a tela inteira funciona (3D, gráficos, configuração).
- **Modo palco:** para apresentar. Os dados vão ao Pd com o menor atraso possível; o 3D, os
  gráficos e os ajustes ficam pausados. Só ficam ativos o estado do Pd, o medidor do sopro e a
  barra de sensibilidade do sopro (escreve o limiar `0x2B` e relê para conferir; se o valor
  não pegou, diz para desbloquear com o PIN no painel Conexão). O modo escolhido fica
  guardado no navegador.

## Abas da Configuração

O painel **Configuração** tem duas abas. **Básico** traz o que se muda para operar o globo
(detector de sopro, navegação, CO₂, nome e PIN). **Avançado** traz o ajuste da cadeia de
sinais e, abaixo dos campos, as **Métricas** (taxa,
jitter, perda, idade da amostra, navegação e o diagnóstico do firmware): são ferramentas de
bancada, que não precisam ficar à vista de quem opera.

O app só oferece o modo **Quat6** (giroscópio + acelerômetro). O magnetômetro (Quat9) e a calibração dele saíram da interface; o firmware continua com o código, acessível por escrita TLV crua.

### Interruptores liga/desliga

Tudo o que liga ou desliga uma função é um interruptor (o estado aparece ao lado:
"Ligado"/"Desligado"); os filtros com posição "desligado" têm um interruptor pequeno antes
do nome. Com um interruptor desligado, os campos que deixam de ter efeito ficam
esmaecidos e travados. Quais são eles segue o que o **firmware** faz, não a seção em que o
campo aparece (`GOVERNORS` em `src/panels/config-panel.ts`):

| Interruptor | Desligado, deixam de valer |
|---|---|
| Áudio › Detector de sopro | tipo de detector, fonte e valor da sensibilidade, medida, rearme, debounce e os alfas do adaptativo. Os microfones continuam medindo (RMS e pico vão para os gráficos e para o Pd), e o passa-banda e o stream cru continuam valendo. |
| Áudio › Filtro passa-banda | high-pass, low-pass e os dois Q |
| CO₂ › Sensor de CO₂ (SC8) | timeout de dado velho, suavização e log bruto. A bola manda 600 ppm no lugar da leitura; o pulso virtual do sopro e o gatilho continuam valendo. |
| Sistema › Log serial (USB) | CO₂ › log bruto (ele escreve no log serial) |

**Sensibilidade (dBFS) e a fonte da sensibilidade.** A linha "Sensibilidade (dBFS)" tem uma
caixa só, na mesma linha do nome, e o que ela mostra depende do interruptor **Fonte da
sensibilidade**:

- **Potenciômetro:** a caixa mostra ao vivo, em dBFS, a sensibilidade que o botão físico da
  bola está dando (−60 a 0, do pacote de sensores) e fica travada. Esse número nunca vira o
  valor do registrador `0x2B`: girar o botão não marca o campo como editado nem entra no
  Aplicar. Antes do primeiro pacote de sensores a caixa fica vazia (—).
- **Valor fixo:** a mesma caixa passa a mostrar o valor gravado no `0x2B` e fica editável.

A troca entre os dois é imediata ao mexer no interruptor, antes mesmo de aplicar.

## Sensores em tempo real

O painel "Sensores em tempo real" mostra o valor atual de cada sensor com a unidade
correta e, abaixo, gráficos de linha empilhados verticalmente que **compartilham o
mesmo eixo X (tempo, em segundos)**, cada gráfico com uma cor própria. Cada cartão tem o
fundo na cor da sua linha nos gráficos. Os cartões dos **microfones** mostram o RMS e o
pico lado a lado, em dBFS inteiros, sobre um medidor de −90 a 0 dBFS: a barra é o RMS
(média da janela configurada, 50 ms por padrão) e o traço é o pico (a amostra mais alta), e o desenho ao lado de cada
rótulo repete a parte do medidor que ele nomeia. O cartão do **potenciômetro** mostra a
posição (%) e a sensibilidade correspondente (−60 a 0 dBFS) numa trilha de fader, e diz se
o botão está em uso pelo detector. **Orientação** (roll, pitch e yaw, em graus) e **aceleração**
(X, Y e Z, em mg) aparecem em três colunas, com sinal explícito (+/−) e sem separador de
milhar. Em todos esses cartões, cada número fica alinhado à direita numa caixa da largura do
maior valor que pode ter (4 caracteres para graus e dBFS, 5 para mg), na fonte monoespaçada:
a casa das unidades não se mexe quando o sinal troca ou o número de dígitos muda. O traço
antes de cada eixo é a cor dele nos gráficos. O cartão do **CO₂** mostra a concentração (ppm) e
o gatilho (ligado/desligado) sobre um medidor na mesma escala do gráfico; o traço marca o nível
do gatilho como está no campo "Gatilho de CO₂" da Configuração (mexe enquanto o valor é
editado e aparece também no demo; se o valor ainda não foi aplicado à bola, a legenda avisa),
e o cabeçalho diz de onde vem o número (leitura real, pulso virtual, sem leitura ou inválido).

- **Janela:** 5, 10, 30 ou 60 s (padrão 10), escolhida acima dos gráficos. Os últimos 60 s
  ficam sempre guardados, então trocar para uma janela maior já mostra o passado.
- **Todos os pacotes:** cada pacote recebido entra no gráfico. Quando a parte visível de uma
  linha passa de 500 pontos (janelas longas ou taxas altas), cada trecho é desenhado pelo seu
  mínimo e máximo: os picos e os pulsos de sopro continuam aparecendo e o redesenho custa o
  mesmo em qualquer janela. Desenhar os 12 000 pontos de 60 s a 200 Hz em cada um dos 10
  redesenhos por segundo travava a página — e, com ela, o envio ao Pd, que roda na mesma
  thread.
- **Faixas fixas do eixo Y** (a escala não pula com o dado): orientação −180…180°; aceleração
  −4000…4000 mg; RMS e pico −120…0 dBFS; detecção de sopro −0,1…1,1; CO₂ 0…máx(5000,
  `virtualPeakPpm`) ppm; potenciômetro 0…100 %; minilinha Taxa 0…1,2 × `imuNotifyHz`;
  minilinha Perda 0…100 %.

| Sensor | Unidade | Derivação |
|---|---|---|
| Orientação (IMU ICM-20948 ou GY-91/MPU-9250) | graus (°) | fusão Madgwick no firmware → quaternion → Euler na convenção legada (mesma saída que o site entrega ao patch Pd) |
| Aceleração (IMU) | mg | já chega em milli-g no pacote de movimento v2 (1000 mg = 1 g; fundo de escala ±4 g = ±4000 mg) |
| Microfones esq./dir. (INMP441) | dBFS (AES17), em dB inteiros no cartão | **RMS e pico medidos no firmware** (janela configurável em Avançado → “Janela do RMS”, 50 ms por padrão; o pico amostrado no 16 kHz cheio) em passos de 0,5 dB (1 byte cada) e enviados em cada pacote de orientação — ou seja, na taxa de orientação, não a 5 Hz. 0 dBFS do RMS = seno de fundo de escala (AES17); 0 dBFS do pico = amostra de fundo de escala, 3,01 dB abaixo para a mesma senoide. |
| CO₂ (SC8-CO2-5K) | ppm | o sensor emite 1 quadro/s sozinho por UART (9600 8N1); o firmware só escuta, nunca transmite (uma consulta pararia o fluxo do sensor). EMA opcional, TLV `0x28`. O pacote de CO₂ também traz o bit de **gatilho** (nível `0x29` com histerese `0x2A`) |
| Potenciômetro | % do curso e dBFS | ADC filtrado 0–4095 → 0–100 % e −60…0 dBFS (a sensibilidade que o botão dá; o limiar efetivo aparece como linha contínua só no gráfico da medida ativa, que segue a chave RMS/Pico da Configuração no instante do clique, antes mesmo de aplicar: "Microfones - RMS" ou "Microfones - Pico"; cada um traz os dois microfones) |

### Sopro: descartar outros sons

Além do passa-banda e da linha, o detector usa três critérios para não confundir sopro com
outros sons graves e fortes (firmware v10). Ficam em **Avançado → "Sopro: descartar outros
sons"**, cada um com seu interruptor (desligado = comportamento de antes):

- **Coerência máxima entre os microfones** (padrão 0,60): som que vem de longe (voz, música,
  uma batida na mesa) chega aos dois microfones como a mesma onda, coerência perto de 1; o
  sopro é turbulência diferente em cada microfone, coerência perto de 0. O sopro só dispara
  com a coerência abaixo do limite. A **distância entre os microfones** (padrão 15,4 mm, M1–M2
  na placa do KiCAD) define os atrasos que a coerência procura.
- **Duração mínima do sopro** (padrão 120 ms): o som precisa ficar acima da linha, sem
  interrupção, por esse tempo. Rejeita batidas e sílabas; todo sopro chega esse tempo depois.
- **Manuseio** (padrão +12 dB): enquanto o IMU vê a bola girando (acima de 30 °/s) ou batendo
  (mais de 300 mg longe de 1 g), e por mais 300 ms, a linha de disparo sobe 12 dB.

O cartão **Sopro: descarte**, no painel Sensores, mostra ao vivo a coerência dos microfones
(barra, de 0 a 1) com o limite da Configuração marcado (o traço mexe enquanto o campo é
editado, antes de aplicar), o estado do detector (abaixo da linha, acima da linha,
aguardando a duração, bloqueado por som de longe) e se a bola está sendo manuseada agora. A
linha de disparo nos gráficos dos microfones já sobe durante o manuseio. Os valores padrão
são estimativas e ainda não foram ajustados com a bola.

O nível efetivo de disparo do detector, o potenciômetro, a coerência e o estado dos critérios
do sopro chegam num pacote próprio (`SensorsPacket`, 5 Hz, na característica de diagnóstico — ver
`firmware/docs/protocol.md`); o envelope dos microfones (RMS e pico por canal) e o bit de
sopro viajam junto com a orientação no `ImuPacket` v2, com o mesmo carimbo de tempo. No modo demo o
painel roda com dados sintéticos.

### Analisador de espectro (ajuste do passa-banda do sopro)

A bola transmite o **áudio bruto decimado** (mono, 2 kHz, int16, ~4,3 kB/s) e o
**FFT roda aqui no navegador** (512 pontos, janela Hann de 256 ms, bins de ~3,9 Hz).
O gráfico mostra o nível em dBFS contra a frequência. Um interruptor **Linear ⟷ Log** no
título troca a escala da frequência (o navegador lembra a escolha): **log, de 10 a 1000 Hz**
(padrão), como num analisador de áudio, para os graves, onde está o sopro, não ficarem
espremidos no canto esquerdo; ou **linear, de 0 a 1000 Hz**.

> **Esse stream vem desligado de fábrica.** Ligue em **Configuração › Avançado › Áudio ›
> Stream de áudio cru (2 kHz)** e aplique; enquanto está desligado o gráfico mostra o
> aviso correspondente. Ele é o item mais caro do enlace (~45 ms/s de tempo de ar,
> ~53 % do total quando ligado) e nada mais depende dele: o RMS e o pico vêm medidos do
> firmware. Desligue ao terminar o ajuste.

Sopre na bola e observe quais frequências sobem. O espectro é o som **antes** do
passa-banda (o que chega aos filtros). Por cima dele o gráfico desenha:

- a **resposta real do passa-banda** (curva grossa, eixo da direita em dB: 0 dB passa
  inteiro, −20 dB sobra um décimo da amplitude), calculada com o mesmo projeto do firmware
  (Butterworth da inclinação escolhida, Q = ganho no corte, transformada bilinear a 16 kHz).
  Medido no firmware compilado no PC, o modelo bate com ele em 463 pontos com diferença
  máxima de 0,03 dB;
- o **espectro após o filtro** (linha azul): o espectro cru somado, em dB, à resposta. É uma
  estimativa do que o detector de sopro recebe, calculada aqui, não medida. Vale para o som
  abaixo de 1 kHz: o detector filtra a 16 kHz, mas o fluxo cru é reduzido a 2 kHz por média
  de 8 amostras, que cai ~4 dB em 1 kHz e dobra o que está entre 1 e 8 kHz para dentro de
  0–1 kHz. Esse som aparece aqui maior do que o detector o recebe;
- uma linha pontilhada em cada corte, com a inclinação e o Q.

Tudo segue os campos **High-pass**, **Low-pass**, **Inclinação** e **Q** da Configuração
enquanto você edita, antes de aplicar; até aplicar (ou se o corte for diferente do que o
firmware ecoa em cada pacote do stream), as duas curvas ficam tracejadas e os cortes
marcados "(não aplicado)". Presets salvos antes das inclinações existirem trazem Q 5,00 (o
padrão antigo) como 0,71, a mesma regra do firmware. Com o
stream cru desligado, só a resposta aparece. Com o passa-banda desligado, a curva e as
linhas somem.

Os filtros rodam **no firmware**, antes do detector, do RMS e do pico. Cada corte tem uma
**inclinação** de 6, 12, 18, 24, 36 ou 48 dB/oitava (Butterworth de ordem inclinação ÷ 6;
padrão 12) e um **Q** que é o ganho exatamente no corte, em qualquer inclinação: 0,71 =
−3 dB (banda plana, o padrão), 1,00 = 0 dB, 5,00 = +14 dB (pico ressonante). Em 6 dB/oit
não existe Q e o campo some. Bolas gravadas com firmware anterior guardavam Q 5,00 (o
padrão antigo): ao ligar com o firmware novo, um Q guardado igual a 5,00 passa a 0,71.

## Navegação do globo

A bola calcula sozinha para onde o globo virtual aponta e envia o resultado num pacote próprio (`NavPacket`, característica `…0006`); o app só o mostra e configura.

- **Configuração › Avançado › Métricas › Navegação:** modo, latitude e longitude do centro da tela, rumo (padrão Mapbox: graus no sentido horário, a partir do norte, da direção que aponta para cima na tela), velocidade angular do globo e "parado" / "em movimento". "Parado" vem da máquina de estados da bola (histerese e tempo de espera), que é a autoridade; o bit "em movimento" é instantâneo e pode divergir dela por um instante.
- **Painel Configuração › Navegação do globo:**
  - **Modo de navegação:** Desligado, **Espelho** (o globo é exatamente a orientação da bola) ou **Joystick** (a inclinação da bola, a partir do zero, faz o globo girar com velocidade crescente com o desvio; devolvida ao zero, ele para).
  - Joystick: velocidade máxima (1–720 °/s), ângulo da velocidade máxima (5–90°) e curva de resposta p (1,0–4,0; 3,0 = cúbica).
  - Taxa de envio (10–200 Hz) e ponto de referência (lat, lon; padrão Campinas/SP). O ponto de referência não entra nos presets.
  - Limiares de partida/parada do Espelho (°/s) e do Joystick (°/s, uma casa decimal) e o tempo para considerar parado (ms). A parada precisa ser menor que a partida, nos dois pares; senão o firmware rejeita a escrita inteira. No Espelho, a velocidade é medida numa janela de ~100 ms (o tremor da mão não conta como movimento) e, parado, o globo volta a "em movimento" depois de um giro de 3°, mesmo lento.
  - Os parâmetros do joystick e os limiares valem na hora e não movem o globo. Só trocar o modo, trocar o ponto de referência ou mudar a referência do IMU (**Zerar orientação**, **Calibrar montagem**) leva o globo de volta ao ponto de referência. Com uma âncora ativa (comando `0x17`, que o site do GaiaSenses envia ao conectar), o globo volta ao ponto da âncora, não ao de referência; trocar o ponto de referência descarta a âncora.
- **Referência e Zerar orientação** (painel Conexão): ligue a bola apoiada numa superfície plana e parada — a postura em que ela liga é a referência, sem “frente” marcada (o movimento é relativo a ela). O zero **não é gravado** e não precisa ser: ao religar, a referência volta a ser a postura do boot. **Zerar orientação** só troca a referência sem religar (bola parada na superfície). O lado da bola voltado para o público no instante da referência define como os gestos aparecem na tela.
- **Gatilho de CO₂** (Configuração › CO₂): nível e histerese do bit de gatilho do pacote de CO₂; Métricas (Configuração › Avançado) mostra o bit na linha CO₂ ("gatilho: ligado/desligado"). O sopro só liga o gatilho se o pico virtual (`0x23`, padrão 2100 ppm) ficar acima do nível.
- **Firmware:** a config-app funciona só com o firmware atual (o `main` deste repositório). Não há caminho de compatibilidade para firmware antigo: grave o firmware atual na bola antes de usá-la.

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
npm run dev        # servidor de desenvolvimento (com --host: acessível na rede local)
npm run build      # gera dist/
npm run preview    # serve dist/ localmente (ou sirva dist/ com qualquer servidor estático)
npm test           # testes (vitest)
```

**Modo demo:** abra `http://localhost:5173/?demo=1` para ver a visualização 3D, as métricas e o painel de sensores com dados sintéticos, sem dispositivo. O demo também simula a leitura da configuração (uma bola falsa com os registradores de fábrica, que guarda o que você aplica), então dá para experimentar a faixa de status, as marcas "na bola: …" e o **Aplicar (n)**. A bola falsa começa desbloqueada; um PIN diferente de `1234` a trava (e ela passa a ignorar o Aplicar), e `1234` destrava.

## Roteiro de bancada

1. **Conectar** (painel Conexão) e escolher o Gaiaball na lista do navegador.
2. **Desbloquear** com o PIN (padrão de fábrica: `1234`). O app relê a bola e diz o resultado: **Desbloqueada**, **PIN incorreto** ou **Bloqueada por 30 s** (depois de 5 PINs errados seguidos); o selo ao lado do botão mostra o estado da conexão, já a partir da leitura automática ao conectar. O desbloqueio vale só para a conexão atual: se a bola reiniciar ou a conexão cair, conecte e desbloqueie de novo.
3. Ao conectar, o app **lê sozinho a configuração da bola** (até 3 tentativas) e preenche o formulário; a faixa no topo do painel Configuração mostra "Lendo…", "Valores lidos da bola às HH:MM:SS · IMU em uso: …" ou, se falhar, "Tentar de novo". **Ler do dispositivo** lê de novo e descarta o que você editou. Um campo editado (ou vindo de um preset) que difere da bola mostra "na bola: …" embaixo, e o botão vira **Aplicar (n)**.
4. Em **superfície plana**, gire a bola e acompanhe **Taxa**, **Perda** e **Jitter** em Configuração › Avançado › Métricas.
5. Acompanhe a vista 3D — ela mostra o quaternion tratado exatamente como chega da bola — e ajuste a Taxa (Hz). Para suavizar o movimento, use **Suavização do quaternion (slerp)** no painel Configuração: como a vista reflete o fluxo real, o efeito do slerp (e o atraso que ele adiciona) aparece nela ao aplicar.
6. **Aplicar**: só os campos alterados são enviados; em seguida o app relê o dispositivo para verificar. Se algum valor voltar ao anterior, aparece o aviso "alguns valores não foram aceitos"; se a bola informar que a conexão está travada, o aviso diz isso e pede o PIN.
7. Se tudo foi aceito, o **Aplicar** já grava na NVS (comando `0x11`), como descrito em "Persistência": a configuração sobrevive a desligar/reiniciar a bola.
8. **Presets**: salve a configuração atual com um nome, carregue (só preenche o formulário; clique em Aplicar para enviar), exclua, exporte/importe JSON. O PIN nunca entra nos presets nem é lido do dispositivo.
