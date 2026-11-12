# Gaiaball — suporte a dois IMUs (ICM-20948 e GY-91/MPU-9250) com seleção por configuração

Data: 2026-11-12 · Branch de destino: `feature/firmware-v2`

## Contexto

Em 2026-11-12 o firmware v2 foi gravado pela primeira vez no hardware real (o caminho de
gravação pelo WSL2 foi resolvido com `usbipd-win`; ver `docs/bench-validation.md`). O boot,
capturado na serial antes de o TLV `0x50` desligar o log, mostrou todo o firmware subindo
corretamente — BLE anunciando `Globo_1`, I2S estéreo a 16 kHz, UART1 do CO₂ em modo escuta —
e **o IMU falhando**:

```
E [mpu] WHO_AM_I 0x48 at 0x68: not an MPU-9250/9255     (10 tentativas, mesmo valor)
E [imu] MPU-9250 (GY-91) not found; IMU disabled
E [app] IMU not available
```

O valor `0x48` é estável nas dez tentativas, então o barramento I²C funciona e há um
dispositivo em `0x68`: ele apenas não é o chip que o driver espera. `kRegWhoAmI = 0x75`
(`Mpu9250.cpp:23`) está correto para um MPU-9250, e a checagem aceita só `0x71`/`0x73`
(`Mpu9250.cpp:77`).

O usuário identificou o módulo desta bola: **`GY-ICM20948V2`**, ou seja um **ICM-20948**.
Isso explica a leitura — no ICM-20948 o WHO_AM_I fica no registro **0x00** (valor `0xEA`) e o
registro `0x75` é reservado, então o firmware leu um byte sem significado, estável porque é o
que está no silício.

A consequência é que a confirmação de hardware de 2026-11-06 ("IMU = GY-91"), que motivou o
commit `57f237c` (port ICM-20948 → MPU-9250, com remoção da biblioteca SparkFun, −16k linhas),
**não vale para esta bola**. O design original do v2
(`2026-11-06-gaiaball-v2-design.md`) descrevia, corretamente, "ESP32 DevKit + IMU ICM-20948".
`firmware/docs/hardware-pinout.md:41-50` já registrava a ambiguidade e a regra para desempatar:
*"a ball that visibly moved under the legacy ICM-20948 firmware must have carried an
ICM-20948"*. A bola em mãos é desse tipo.

E há um fato novo de produto: **as próximas GAIAballs serão fabricadas com GY-91
(MPU-9250)**. Portanto o firmware precisa servir às duas linhas, com o chip escolhido por
configuração — não por compilação, não por adivinhação.

## Objetivos

1. **Um único binário** atende às duas variantes de hardware.
2. O chip é escolhido **no config-app**, por unidade, e persiste na NVS.
3. Uma bola sem configuração mantém o **IMU desligado** — um estado explícito e legítimo, não
   um erro e não uma tentativa de sondagem.
4. Trocar o chip **exige reiniciar**, com aviso claro na UI e um botão que reinicia a bola ali
   mesmo.
5. O **formato do protocolo não muda**: nada no site do GaiaSenses nem no objeto Pure Data
   precisa saber que existem dois sensores.
6. Toda a cadeia de sinal do v2 (`Ahrs`, `GyroBias`, `MagCalibration`, `QuatSmoother`,
   `Hampel5`) é **reaproveitada sem alteração de comportamento**.
7. O **remapeamento de eixos do magnetômetro vira configuração**, não constante de código, para
   que o usuário feche na bancada sem depender de uma versão nova de firmware — e para que a
   linha GY-91, cujo hardware ainda não existe, possa ser corrigida depois sem release.

## Não-objetivos

- Autodetecção no caminho principal. Decisão explícita do usuário: sem configuração, o IMU
  fica parado. (Uma sondagem *sob demanda*, disparada pelo operador, está fora deste escopo —
  ver "Ideias deliberadamente adiadas".)
- Voltar à biblioteca SparkFun ou ao DMP. A fusão roda no ESP32 e continua assim.
- Ler o BMP280 do GY-91 (0x76) ou o termômetro de qualquer dos dois chips.
- Suportar um terceiro IMU, ou troca a quente sem reiniciar.
- Mexer no comportamento da fusão, do rastreador de viés ou da calibração magnética.
- **Colocar a cadeia de orientação de pé na bancada.** Decisão explícita do usuário: este
  trabalho entrega o encanamento (driver, seleção, testes). Nada da cadeia do v2 — fusão, viés
  de giro, montagem, zero — jamais recebeu uma amostra de hardware, e se a orientação sair
  torta depois que o driver estiver entregando dados, isso é uma tarefa separada. Em
  consequência, **não** entra uma vista de eixos crus (accel/gyro/mag antes da fusão) no
  config-app: foi oferecida e recusada.

## Anti-objetivos (o que seria fracasso mesmo "funcionando")

- **Uma bola inacessível** por causa de uma seleção errada. O BLE precisa subir sempre,
  independente do IMU — como já acontece hoje.
- **Mudar o formato dos pacotes** e obrigar o site e o Pd a acompanhar.
- **Falha silenciosa**: o operador vê "o modelo 3D não mexe" sem conseguir distinguir chip
  errado, sensor solto e configuração ainda não feita.
- **A fusão aprender qual é o chip.** `Ahrs` consome dps, g e µT (`Ahrs.h:52`); se um `if` de
  modelo de sensor aparecer em `logic/`, o design falhou.
- Duplicar a lógica de taxa/DLPF/escala em dois drivers que divergem com o tempo.

## Restrições

- ESP32 DevKit; PlatformIO `espressif32@6.12.0` **e** arduino-cli (toolchain dupla obrigatória).
  Hoje: Flash 62,1 %, RAM 15,7 %.
- `ImuService` roda numa task FreeRTOS própria, com polling adaptativo (período/4, limitado a
  2–5 ms, porque o pino INT não é roteado na placa) e watchdog de 10 s.
- Codec TLV com aplicação leniente; NVS em schema v6; buffer TLV de 384 B.
- `DiagPacket` tem 34 B e **`ahrsFlags` está com os 8 bits ocupados**
  (`Packets.h:50-57`) — não há bit sobrando para o estado do IMU.
- config-app: TypeScript + vitest; `Field` exige `info` (tooltip didático) em todo campo
  (`config-panel.ts:7-20`); abas Básico/Avançado; `FIELD_DEFAULTS` espelha `GaiaConfig.h` com
  teste de paridade.
- Suíte atual: **131 testes nativos + 52 vitest**, ambos verdes, nas duas toolchains. Os testes
  nativos compilam só `logic/` (sem hardware).
- `firmware-original.ino` e `relatorio.md` são referências históricas e **nunca** devem ser
  modificados.

## Decisões tomadas com o usuário

| Tema | Decisão |
|---|---|
| Onde se escolhe o chip | No config-app, como mais um campo de configuração |
| Padrão de fábrica | *Não configurado* → IMU desligado, sem sondagem |
| Efeito da troca | Exige reiniciar; a UI precisa **avisar isso claramente** |
| Verificação na bancada | O usuário testa pela própria Web UI (seleciona, reinicia, sacode a bola, vê o modelo 3D) |
| Fabricação futura | GY-91 (MPU-9250) nas próximas unidades; a bola atual é ICM-20948 |
| Escopo | Só o encanamento. Orientação torta na bancada vira tarefa separada; sem vista de eixos crus |
| Eixos do magnetômetro | **Configurável pela Web UI**, não constante de código |

## Decisões do autor deste documento (não do usuário)

Todas foram verificadas contra o código antes de entrar aqui.

- **Linha de estado do IMU no painel Métricas**: *não configurado* / *ICM-20948 ok* /
  *selecionado X, não encontrado* / *trocado, aguardando reinício*. Motivo: na bancada, "o
  modelo não mexeu" é ambíguo entre causas distintas, e dez bolas em sequência multiplicam o
  custo dessa ambiguidade. O usuário aprovou com a ressalva de que pode ser removida se poluir.
- **O que o `imuDetected` carrega**: a **seleção vigente no boot** mais um bit de
  encontrado/não — não apenas "o que foi achado". Sem a seleção de boot é impossível separar
  "chip ausente" de "trocado e ainda não reiniciado": a escrita do TLV vale em RAM na hora, a
  leitura devolve a configuração viva, e qualquer releitura antes do reboot mostraria
  `imuChip=2, imuDetected=0` numa bola cujo IMU nunca foi consultado. Esse é justamente o
  instante em que a linha de estado é usada. Ids são permanentes neste projeto, então errar
  aqui é caro de desfazer.
- **`imuDetected` fica FORA da tabela de registros do codec**: anexado pelo caminho de leitura
  da configuração, nunca aplicado, nunca serializado para a NVS. Duas razões concretas:
  `tlvApply` é tudo-ou-nada (um registro inválido derruba a escrita inteira, e o firmware não
  tem como dizer qual foi), e `ConfigStore::save()` usa a mesma tabela — um registro
  somente-leitura dentro dela seria gravado na NVS e recarregado no boot seguinte, fazendo a
  linha de estado reportar o resultado do boot ANTERIOR.
- **Ids TLV**: `0x34` = `imuChip`, `0x35` = `imuDetected` (somente-leitura), `0x36` =
  `magAxisRemap`. Bloco de hardware que começa em `0x30` (pinos de LED). `0x21`, `0x27` e
  `0x33` continuam reservados e não reutilizáveis.
- **Valores de `imuChip`**: `0` = não configurado (padrão), `1` = ICM-20948, `2` =
  GY-91/MPU-9250. O zero como "não configurado" faz o padrão de fábrica cair naturalmente no
  estado que o usuário pediu.
- **`imuChip` sai dos presets.** Hoje "Salvar preset" varre `for (const f of FIELDS)` sem
  filtro, e presets existem exatamente para mover ajustes **entre bolas** — um preset capturado
  na bola ICM e carregado numa GY-91 reescreveria a identidade do hardware em silêncio, com o
  estrago só aparecendo no reboot seguinte. O PIN e a calibração magnética já são excluídos
  nominalmente pelo mesmo motivo; `imuChip` e `magAxisRemap` entram na mesma lista.
- **Os rótulos do DLPF passam a seguir o chip selecionado.** Hoje o select é estático com os
  cortes do MPU-9250 (`184/92/41/20/10/5 Hz`) e o tooltip diz "Passa-baixas interno do
  MPU-9250"; num ICM-20948 esses mesmos índices valem outra coisa, então a UI mentiria para o
  operador — o anti-objetivo da falha silenciosa. O semântico do registro continua sendo
  "índice de DLPF"; só a apresentação muda. `Field.options` é um array estático hoje e precisa
  aceitar uma função da configuração corrente. Mesma correção nos tooltips e nas linhas de
  `protocol.md` que dizem "MPU-9250" (`0x02`, `0x05`, `0x06`, `0x0C`).
- **A aritmética de taxa/DLPF dos DOIS drivers vai para `logic/`**, atrás de um helper único
  com teste nativo. O documento antes afirmava que isso "já é assim no MPU-9250" — não é:
  `autoDlpfMode()` e o cálculo do `SMPLRT_DIV` vivem num namespace anônimo dentro de
  `services/Mpu9250.cpp`, o filtro do `platformio.ini` exclui `src/services/` do ambiente
  `native`, e não existe `test/test_imu`. Como o portão sem-hardware é obrigatório e **não há
  hardware GY-91 para testar na bancada**, mover os dois é a única forma de o caminho MPU-9250
  ter qualquer verificação.
- **O config-app passa a ler a configuração automaticamente ao conectar.** Hoje `readConfig()`
  só roda no botão "Ler do dispositivo" e depois de Aplicar (`config-panel.ts:471,486`), e
  `populate()` descarta registro sem `Field` — então "lido junto com a configuração ao
  conectar" não descreve o app atual. Mexe na guarda de sujo/snapshot do painel.
- **Aba do config-app**: **Básico** para `imuChip` (identidade de hardware, não afinação);
  `magAxisRemap` em **Avançado**.
- **Nomenclatura na UI**: "GY-91 (MPU-9250)" e "ICM-20948", como o usuário fala dos módulos.

## Forma da solução

### Firmware

O acoplamento ao chip hoje é mínimo: `Mpu9250` só é referenciado por `ImuService.h` e
`ImuService.cpp`, e sua interface pública é pequena e já está na forma certa:

```
begin(wire, ad0) · setSampleRateHz(hz, dlpfMode) · setGyroFullScale(fsSel)
dataReady() · readAccelGyro(acc[3], gyro[3]) · readMag(uT[3])
magOk() · gyroLsbPerDps() · gyroRangeDps() · dlpfCutoffHz() · dlpfMode() · whoAmI() · errorCount()
```

Extrair essa interface (`ImuDriver`, despacho em tempo de execução) e fornecer duas
implementações: a `Mpu9250` existente e uma `Icm20948` nova. `ImuService` passa a receber
qual construir, a partir da configuração, e mantém tudo o mais: polling adaptativo, watchdog,
`ImuSettings`, contagem de erros I²C.

As escalas foram escolhidas de modo a **preservar o formato do fio**: ±4 g = 8192 LSB/g e
±250/500/1000/2000 dps existem nos dois chips, com os mesmos códigos de seleção.

Duas coisas saem de onde estão hoje: a aritmética de taxa/DLPF dos dois drivers vai para
`logic/` (ver "Decisões do autor"), e o remapeamento de eixos do magnetômetro sai da constante
literal em `ImuService.cpp:314` (`{uT[1], uT[0], -uT[2]}`) e vira configuração.

### Deltas reais do ICM-20948 (o trabalho de verdade)

O construtor deve conferir cada endereço contra o datasheet **DS-000189 (ICM-20948, rev. 1.3)**;
os valores abaixo orientam o esforço, não substituem a leitura:

1. **Bancos de registro.** `REG_BANK_SEL` (0x7F, visível em todos os bancos, nº nos bits 5:4).
   Dados e controle em banco 0; `GYRO_SMPLRT_DIV`, `GYRO_CONFIG_1/2`, `ACCEL_SMPLRT_DIV_1/2`,
   `ACCEL_CONFIG` em banco 2; mestre I²C em banco 3. O MPU-9250 não tem bancos — é a maior
   diferença estrutural, e pede um helper que troque de banco uma vez por operação, nunca por
   registro.
2. **O chip acorda dormindo.** `PWR_MGMT_1` (banco 0, 0x06) tem `SLEEP` ligado no reset; é
   preciso limpá-lo e escolher `CLKSEL = 1`. O `Mpu9250` atual já faz o equivalente.
3. **Taxas separadas.** Gyro: `ODR = 1125 / (1 + GYRO_SMPLRT_DIV)`; acelerômetro:
   `ODR = 1125 / (1 + ACCEL_SMPLRT_DIV)`, com divisor de **16 bits** em dois registros. O
   MPU-9250 tem um divisor só para os dois. `setSampleRateHz` precisa programar os dois e
   manter a promessa atual da interface (um valor de saída, filtro anti-alias casado).
4. **DLPF com tabela própria.** `GYRO_DLPFCFG` e `ACCEL_DLPFCFG` (3 bits cada) têm cortes
   diferentes dos do MPU-9250, e cada um tem seu bit `FCHOICE` para desviar do filtro. A
   regra do modo automático ("o DLPF mais largo ainda abaixo do Nyquist da saída") é a mesma;
   só muda a tabela.
5. **Leitura dos dados.** Acelerômetro em `ACCEL_XOUT_H` (banco 0, 0x2D) e giroscópio logo em
   seguida, contíguos — uma leitura só pega os 12 bytes. No MPU-9250 há o termômetro no meio.
6. **Pronto-para-ler.** `INT_STATUS_1` (banco 0) tem `RAW_DATA_0_RDY_INT`, equivalente ao
   `INT_STATUS` que o driver atual lê e limpa na leitura.
7. **Magnetômetro AK09916, não AK8963.** Mesmo endereço (0x0C) e mesmo `WIA1 = 0x48` em 0x00
   — por isso a checagem de magnetômetro do código atual passaria nos dois —, mas: `WIA2`
   (0x01) = 0x09; **não existem registros ASA / fuse ROM**, a sensibilidade é fixa em
   **0,15 µT/LSB** (o `magAdj_[3]` do `Mpu9250` simplesmente não se aplica); modos de medição
   contínua em `CNTL2` (0x31); `ST2` (0x18) precisa ser lido para encerrar cada medição. Para
   falar direto com ele, `BYPASS_EN` em `INT_PIN_CFG` com o mestre I²C desligado em
   `USER_CTRL` — mesma estratégia que o driver atual usa com o AK8963.
8. **Remapeamento de eixos do magnetômetro.** O driver atual aplica X↔Y / −Z para o AK8963.
   O relacionamento no ICM-20948 **deve ser verificado na bancada**, não presumido igual:
   apontar a bola para o norte magnético e conferir o heading, com a calibração de hard/soft
   iron zerada. Esta é a única parte do trabalho que não se fecha sem hardware em mãos.

### Configuração e protocolo

- **`imuChip`** (TLV `0x34`, u8): `0` não configurado · `1` ICM-20948 · `2` GY-91/MPU-9250.
  Qualquer outro valor é rejeitado.
- **`imuDetected`** (TLV `0x35`, u8, somente-leitura, **fora da tabela do codec**): seleção
  vigente no boot nos bits baixos + bit de encontrado. Permite à UI separar os quatro estados,
  inclusive "trocado, aguardando reinício".
- **`magAxisRemap`** (TLV `0x36`, u8): índice numa tabela das 24 permutações de eixos com sinal
  que preservam a mão direita. O padrão reproduz exatamente o comportamento atual (X↔Y, −Z),
  então nenhuma bola muda de comportamento ao atualizar. Valor fora da faixa é rejeitado.
- **Interação que precisa estar documentada na UI**: `ImuService.cpp:315` publica as amostras
  do `MagChunkPacket` **já remapeadas**, e é sobre elas que o ajuste de hard/soft iron roda no
  navegador. Portanto **mudar o remap invalida a calibração magnética existente** — o tooltip
  precisa dizer isso, e a ordem correta na bancada é fixar o remap primeiro, calibrar depois.
- **Bump do schema da NVS** para v7. Como a persistência já é TLV leniente desde v6, nenhuma
  configuração existente é descartada: um blob sem `0x34` carrega com o campo no padrão, que é
  exatamente "não configurado".
- **Nenhum pacote muda de tamanho ou de layout.** Enquanto o IMU estiver desconfigurado, o
  `ImuPacket` simplesmente não é emitido — o site não usa nada que dependa dele para conectar,
  e o Pd mantém o caminho de áudio pelo `SensorsPacket` a 5 Hz; a única perda é o envelope de
  microfone em alta taxa, que viaja no `ImuPacket`.

### Config-app

- Campo **IMU** (`select`, aba Básico) com `info` explicando o chip, por que a bola sai de
  fábrica sem IMU e que trocar exige reiniciar.
- **Aviso de reinício** visível ao mudar a seleção — não escondido no tooltip —, terminando num
  botão que dispara o comando `0x13` já existente. Não precisa de "salve primeiro": Aplicar já
  envia o `0x11` depois de conferir a releitura.
- Campo **Eixos do magnetômetro** (`select`, aba Avançado), com o aviso de que mudá-lo invalida
  a calibração magnética.
- **Estado do IMU** no painel Métricas, derivado de `imuDetected` × `imuChip`.
- **Leitura automática da configuração ao conectar** (hoje não existe).
- `imuChip` e `magAxisRemap` **excluídos dos presets**.
- Rótulos do DLPF seguindo o chip selecionado.
- `FIELD_DEFAULTS` ganha as entradas novas (o teste de paridade com `GaiaConfig.h` cobre).

### Documentação

- `firmware/docs/hardware-pinout.md:41-50` precisa ser **corrigido**: hoje afirma que o IMU é um
  GY-91 "confirmado pelo usuário". Passa a registrar as duas variantes, a evidência de
  2026-11-12 (WHO_AM_I 0x48 = registro reservado do ICM-20948) e qual bola é qual.
- `firmware/README.md`: cadeia de processamento idêntica para os dois chips + tabela de
  diferenças.
- `docs/bench-validation.md`: passo de seleção do IMU antes de qualquer teste de orientação; a
  ordem remap-antes-de-calibrar; e **o caminho de gravação por usbipd-win, que ainda não está
  lá** (este documento o citava como se estivesse).
- `firmware/docs/protocol.md`: TLV novos, ids reservados, e correção das linhas que falam em
  "MPU-9250" como se fosse o único chip.
- `relatorio/index.html`: achado novo (nova versão do artifact).

## Verificação

O escopo é o encanamento, então o critério de aceite **não** é a orientação sair certa na
bancada — é o driver entregar amostras e a seleção funcionar.

**Sem hardware** (portão obrigatório antes de entregar): escolha de DLPF e divisores dos dois
chips testadas como função pura em `logic/`; tabela das 24 permutações de eixos testada
(determinante +1, ida e volta); validação de `0x34` e `0x36`; `0x35` rejeitado na escrita e
ausente da NVS; configuração pré-v7 carregando como "não configurado"; `imuChip` ausente de um
preset salvo; paridade `FIELD_DEFAULTS` × `GaiaConfig.h`; as duas toolchains compilando; os 131
testes nativos + 52 vitest seguem verdes, mais os novos.

**Com hardware** (o usuário, pela Web UI), na bola ICM-20948: selecionar o chip, reiniciar pelo
botão do aviso, confirmar a linha de estado, e ver o modelo 3D **reagir** ao movimento. Reagir,
não necessariamente reagir certo: eixo trocado, sinal invertido ou deriva são tarefa separada,
por decisão do usuário. Confirmar também que uma bola com seleção errada continua conectável e
corrigível pela UI, e que a seleção sobrevive ao desliga-liga.

Na linha GY-91, quando existir hardware: o mesmo roteiro. Até lá esse caminho tem apenas a
verificação de host, e **isso precisa estar dito em voz alta na entrega** — o driver MPU-9250
continua sem nunca ter conversado com um MPU-9250.

## Ideias deliberadamente adiadas

- **Vista de eixos crus** no config-app (accel/gyro/mag antes da fusão). Oferecida e recusada:
  o escopo é o encanamento. Custo conhecido dessa escolha: quando a orientação sair torta, não
  haverá como distinguir driver de montagem de fusão sem um ciclo de regravação. O painel de
  calibração magnética já mostra amostras cruas do magnetômetro e cobre parte disso.
- **Botão "Detectar"** na UI, sondando os dois WHO_AM_I sob comando do operador. Não conflita
  com a decisão de não autodetectar (é disparado por gente, não pelo boot) e tornaria a bancada
  de dez bolas bem mais rápida.
- Ler o BMP280 do GY-91, disponível em 0x76 nas unidades futuras.
- Reinicializar o IMU a quente, sem reboot.
