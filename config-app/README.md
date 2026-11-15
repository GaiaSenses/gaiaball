# Gaiaball Config

App web (Vite + TypeScript) para configurar e testar o Gaiaball v2 via Web Bluetooth: visualização 3D da orientação (o quaternion tratado, exatamente como chega por BLE), métricas do fluxo (taxa, perda, jitter), parâmetros do firmware, presets e um painel de **sensores em tempo real** com gráficos de linha (Plotly.js). Ele também repassa os dados da bola ao Pure Data (veja [Enviar ao Pd](#enviar-ao-pd)).

Endereço publicado, provisório: <https://gaiasenses.github.io/gaiaball/> (é o que o workflow do GitHub Pages publica, depois que as Pages forem habilitadas no repositório; quando o domínio final existir, o endereço troca).

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

## Modo ajuste e modo palco

A chave **Modo ajuste ⟷ Modo palco** fica no topo.

- **Modo ajuste:** a tela inteira funciona (3D, gráficos, configuração).
- **Modo palco:** para apresentar. Os dados vão ao Pd com o menor atraso possível; o 3D, os
  gráficos e os ajustes ficam pausados. Só ficam ativos o estado do Pd, o medidor do sopro e a
  barra de sensibilidade do sopro (escreve o limiar `0x2B` e relê para conferir; se o valor
  não pegou, diz para desbloquear com o PIN no painel Conexão). O modo escolhido fica
  guardado no navegador.

## Sensores em tempo real

O painel "Sensores em tempo real" mostra o valor atual de cada sensor com a unidade
correta e, abaixo, gráficos de linha empilhados verticalmente que **compartilham o
mesmo eixo X (tempo, em segundos)**, cada gráfico com uma cor própria.

- **Janela:** 5, 8 ou 12 s (padrão 12), escolhida acima dos gráficos.
- **Todos os pacotes:** os gráficos mostram cada pacote recebido, sem decimar. O rodapé diz
  "Últimos N s, com todos os pacotes recebidos. Os gráficos são redesenhados 10 vezes por
  segundo; isso não atrasa o envio ao Pd nem o site."
- **Faixas fixas do eixo Y** (a escala não pula com o dado): orientação −180…180°; aceleração
  −4000…4000 mg; RMS e pico −120…0 dBFS; detecção de sopro −0,1…1,1; CO₂ 0…máx(5000,
  `virtualPeakPpm`) ppm; potenciômetro 0…100 %; minilinha Taxa 0…1,2 × `imuNotifyHz`;
  minilinha Perda 0…100 %.

| Sensor | Unidade | Derivação |
|---|---|---|
| Orientação (IMU ICM-20948 ou GY-91/MPU-9250) | graus (°) | fusão Madgwick no firmware → quaternion → Euler na convenção legada (mesma saída que o site entrega ao patch Pd) |
| Aceleração (IMU) | mg | já chega em milli-g no pacote de movimento v2 (1000 mg = 1 g; fundo de escala ±4 g = ±4000 mg) |
| Microfones esq./dir. (INMP441) | dBFS (AES17) + dB SPL estimado | **RMS e pico medidos no firmware** (janela de 20 ms; o pico amostrado no 16 kHz cheio) em passos de 0,5 dB (1 byte cada) e enviados em cada pacote de orientação — ou seja, na taxa de orientação, não a 5 Hz. 0 dBFS do RMS = seno de fundo de escala (AES17); 0 dBFS do pico = amostra de fundo de escala, 3,01 dB abaixo para a mesma senoide. SPL ≈ dBFS + 120 (sensibilidade do INMP441: −26 dBFS @ 94 dB SPL, 1 kHz) |
| CO₂ (SC8-CO2-5K) | ppm | o sensor emite 1 quadro/s sozinho por UART (9600 8N1); o firmware só escuta, nunca transmite (uma consulta pararia o fluxo do sensor). EMA opcional, TLV `0x28`. O pacote de CO₂ também traz o bit de **gatilho** (nível `0x29` com histerese `0x2A`) |
| Potenciômetro | % do curso | ADC filtrado 0–4095 → 0–100 % (o limiar mapeado aparece no cartão e como linha pontilhada nos gráficos dos microfones) |

O nível efetivo de disparo do detector, o potenciômetro e as flags de saúde chegam num pacote
próprio (`SensorsPacket`, 5 Hz, na característica de diagnóstico — ver
`firmware/docs/protocol.md`); o envelope dos microfones (RMS e pico por canal) e o bit de
sopro viajam junto com a orientação no `ImuPacket` v2, com o mesmo carimbo de tempo. No modo demo o
painel roda com dados sintéticos.

### Analisador de espectro (ajuste do passa-banda do sopro)

A bola transmite o **áudio bruto decimado** (mono, 2 kHz, int16, ~4,3 kB/s) e o
**FFT roda aqui no navegador** (512 pontos, janela Hann de 256 ms, bins de ~3,9 Hz,
0–1000 Hz).

> **Esse stream vem desligado de fábrica.** Ligue em **Configuração › Avançado › Áudio ›
> Stream de áudio cru (2 kHz)** e aplique; enquanto está desligado o gráfico mostra o
> aviso correspondente. Ele é o item mais caro do enlace (~45 ms/s de tempo de ar,
> ~53 % do total quando ligado) e nada mais depende dele: o RMS e o pico vêm medidos do
> firmware. Desligue ao terminar o ajuste.

Sopre na bola e observe quais frequências sobem; as linhas tracejadas
mostram os cortes HP/LP configurados (ecoados pelo firmware em cada pacote). Os
filtros (HP 10 Hz / LP 200 Hz, Q 5,00 por padrão) são aplicados **no firmware**,
antes do detector de pico — o espectro mostra o sinal **cru**, justamente para
escolher os cortes. Atenção: Q > 0,71 cria um pico ressonante de ~20·log₁₀(Q) dB
exatamente no corte; a inclinação da atenuação vem da ordem do filtro, não do Q.

## Navegação do globo

A bola calcula sozinha para onde o globo virtual aponta e envia o resultado num pacote próprio (`NavPacket`, característica `…0006`); o app só o mostra e configura. Firmware antigo não tem essa característica: o painel Métricas avisa "sem característica de navegação".

- **Painel Métricas › Navegação:** modo, latitude e longitude do centro da tela, rumo (padrão Mapbox: graus no sentido horário, a partir do norte, da direção que aponta para cima na tela), velocidade angular do globo e "parado" / "em movimento". "Parado" vem da máquina de estados da bola (histerese e tempo de espera), que é a autoridade; o bit "em movimento" é instantâneo e pode divergir dela por um instante.
- **Painel Configuração › Navegação do globo:**
  - **Modo de navegação:** Desligado, **Espelho** (o globo é exatamente a orientação da bola) ou **Joystick** (a inclinação da bola, a partir do zero, faz o globo girar com velocidade crescente com o desvio; devolvida ao zero, ele para).
  - Joystick: velocidade máxima (1–720 °/s), ângulo da velocidade máxima (5–90°) e curva de resposta p (1,0–4,0; 3,0 = cúbica).
  - Taxa de envio (10–200 Hz) e ponto de referência (lat, lon; padrão Campinas/SP). O ponto de referência não entra nos presets.
  - Limiares de partida/parada do Espelho (°/s) e do Joystick (°/s, uma casa decimal) e o tempo para considerar parado (ms). A parada precisa ser menor que a partida, nos dois pares; senão o firmware rejeita a escrita inteira. No Espelho, a velocidade é medida numa janela de ~100 ms (o tremor da mão não conta como movimento) e, parado, o globo volta a "em movimento" depois de um giro de 3°, mesmo lento.
  - Os parâmetros do joystick e os limiares valem na hora e não movem o globo. Só trocar o modo, trocar o ponto de referência ou mudar a referência do IMU (**Zerar orientação**, **Calibrar montagem**, Quat6 ↔ Quat9) leva o globo de volta ao ponto de referência.
- **Referência e Zerar orientação** (painel Conexão): ligue a bola apoiada numa superfície plana e parada — a postura em que ela liga é a referência, sem “frente” marcada (o movimento é relativo a ela). O zero **não é gravado** e não precisa ser: ao religar, a referência volta a ser a postura do boot. **Zerar orientação** só troca a referência sem religar (bola parada na superfície). O lado da bola voltado para o público no instante da referência define como os gestos aparecem na tela.
- **Gatilho de CO₂** (Configuração › CO₂): nível e histerese do bit de gatilho do pacote de CO₂; o painel Métricas mostra o bit na linha CO₂ ("gatilho: ligado/desligado"). O sopro só liga o gatilho se o pico virtual (`0x23`, padrão 2100 ppm) ficar acima do nível.
- **Firmware antigo:** **Aplicar** só envia os registradores que a bola informou na leitura (o PIN sempre vai). Um firmware sem navegação recusaria a escrita inteira por causa de um registrador desconhecido; os que ficaram de fora são listados na mensagem.

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

**Modo demo:** abra `http://localhost:5173/?demo=1` para ver a visualização 3D, as métricas e o painel de sensores com dados sintéticos, sem dispositivo.

## Roteiro de bancada

1. **Conectar** (painel Conexão) e escolher o Gaiaball na lista do navegador.
2. **Desbloquear** com o PIN (padrão de fábrica: `1234`). O firmware não confirma o PIN; sem ele, comandos e escritas sensíveis são ignorados.
3. **Ler do dispositivo** (painel Configuração) para preencher o formulário.
4. Em **superfície plana**, gire a bola e acompanhe **Taxa**, **Perda** e **Jitter** no painel Métricas.
5. Acompanhe a vista 3D — ela mostra o quaternion tratado exatamente como chega da bola — e ajuste Modo e Taxa (Hz). Para suavizar o movimento, use **Suavização do quaternion (slerp)** no painel Configuração: como a vista reflete o fluxo real, o efeito do slerp (e o atraso que ele adiciona) aparece nela ao aplicar.
6. **Aplicar**: só os campos alterados são enviados; em seguida o app relê o dispositivo para verificar. Se algum valor voltar ao anterior, aparece o aviso "alguns valores não foram aceitos (desbloqueou com o PIN?)".
7. **Salvar no dispositivo** (painel Conexão) para persistir. "Aplicar" vale somente até reiniciar.
8. **Presets**: salve a configuração atual com um nome, carregue (só preenche o formulário; clique em Aplicar para enviar), exclua, exporte/importe JSON. O PIN nunca entra nos presets nem é lido do dispositivo.
