# Gaiaball: aceleração em mg, detector de sopro, gráficos, ponte para o Pd e modo palco — Plano de implementação

> **Para agentes:** sub-skill obrigatória: superpowers:subagent-driven-development (recomendada) ou superpowers:executing-plans, tarefa por tarefa. Os passos usam checkbox (`- [ ]`).

**Objetivo:** quem não programa liga a bola, abre a config-app no Chrome, cria `[gaiaball]` no Pd e recebe os dados por canal, sem terminal; junto vêm a aceleração em mg, o detector de sopro escolhível, gráficos honestos e o modo palco.

**Arquitetura:** a bola passa a mandar a aceleração em mg (pacote de movimento versão 2) e um bit de sopro. A config-app repassa os pacotes binários, sem alterar e antes de desenhar, por WebSocket para `127.0.0.1`. Um external em C (`[gaiaball]`) decodifica e publica nos nomes `gaia.*` do site. O site GaiaSenses passa a ler a versão 2.

**Tecnologias:** firmware ESP32 em C++ (PlatformIO, testes Unity nativos); config-app em Vite + TypeScript (vitest, ambiente node, sem DOM); external Pd em C99 (`pd-lib-builder`); site em Next.js (`node --test`); GitHub Actions.

**Spec:** `/home/nyck/cti/gaiaball/docs/superpowers/specs/2026-11-14-pd-via-config-app-design.md` (commit 56fd7da). Quem executa lê o spec e este plano.

**Repos e branches:**
- `gaiaball` em `feat/pd-via-config-app`, criada a partir de `feat/navegacao-globo` (PR #3).
- `Gaiasenses-web` numa branch nova `feat/gaiaball-mg`, criada a partir de `feat/gaiaball-navegacao` (PR #239).

## Contexto

Esta conversa fechou o design em várias rodadas:
- caminho D: config-app → WebSocket → `[gaiaball]`;
- aceleração em `int16` mg;
- detector absoluto ou adaptativo, com rearme em dB inteiros;
- faixas fixas no eixo Y e janela de tempo de 5, 8 ou 12 s;
- todos os pacotes desenhados nos gráficos;
- modo palco e modo ajuste;
- tutorial do Deken (já escrito: `pd-gaiaball/PUBLICAR-NO-DEKEN.md`).

Este plano transforma o spec em tarefas sequenciais. Cada tarefa traz as evidências do código atual (arquivo:linha) e a decisão de interface que quem implementa não consegue tomar sozinho.

## Ajustes ao spec feitos neste plano (o usuário confere)

1. **`0x2B` guardado sem sinal.** Fica como `u16` = −dBFS × 100 (0…9000; 3550 = −35,5 dBFS), e não como `i16`. Motivo: nem o `TlvCodec` do firmware (`Kind::U8/U16/U32`, `TlvCodec.cpp:9-15`) nem o `tlv.ts` da config-app (`Spec`, `tlv.ts:80-83`; `value < 0` é rejeitado em `:172`) têm tipo com sinal. A UI continua mostrando e editando −35,5 dBFS. O registrador passa a se chamar `breathThresholdNegDbX100`.
2. **Versão do esquema da NVS 7 → 8.** É o precedente das aposentadorias anteriores: `0x21` em `4a75501` e `0x33` em `b1ca444`.
3. **A config-app ganha um codificador binário** (`src/protocol/encode.ts`). O `?demo=1` hoje despacha objetos já decodificados (`main.ts:180-182`); com o codificador, o demo passa a alimentar o Pd e a tela pelo mesmo caminho dos pacotes reais.
4. **Escrita da sensibilidade no palco.** A barra do palco escreve o `0x2B` e lê de volta. Se o valor não pegou (bola sem PIN desbloqueado; o firmware não confirma escritas), a barra diz "Desbloqueie com o PIN no painel Conexão".
5. **Áudio do pacote de movimento em u8 meio-dB** (decisão do usuário, depois do spec). RMS e pico de cada microfone passam de `int16` dBFS×100 para `u8 = round((dBFS + 120) × 2)`, com faixa de −120 a +7,5 dBFS e passo de 0,5 dB. O pacote v2 cai de 31 para **27 B** (MTU ≥ 30), o áudio vai para os offsets 23–26, e o ar por pacote cai 32 µs. A Tarefa 0 registra isso no spec.
6. **Sem lista de origens no `[gaiaball]`** (decisão do usuário). O spec previa aceitar só `https://gaiasenses.github.io` e `localhost`, mas o domínio final ainda não existe. O objeto aceita qualquer origem. A proteção que sobra é escutar só no próprio computador (`127.0.0.1`) e o pedido "Apps neste dispositivo" do Chrome. O firmware nunca teve restrição de origem.

## Restrições globais (valores exatos do spec)

- **Pacote de movimento v2:** `version = 2`, `type = 0x01`, **27 B**, MTU ≥ 30.
  - Aceleração `int16` em mg nos offsets 16/18/20, `= lroundf(contagens × 1000 / 8192)`.
  - `flags` no offset 22; bit 4 = sopro reconhecido (travado até ser enviado).
  - Áudio em `u8` meio-dB: RMS E/D nos offsets 23/24 e pico E/D em 25/26; `valor = clamp(lround((dbX100 + 12000) / 50), 0, 255)`, `dBFS = valor / 2 − 120`; 0 = piso (−120).
  - Os demais pacotes continuam com `version = 1`.
- **`SensorsPacket`** (23 B, inalterado): `audioThreshold` = nível efetivo de disparo, em contagens da medida ativa. `flags` bit 4 = `metricPeak`, bit 5 = `adaptive`. `potRaw` não muda.
- **TLVs:**

  | Id | Nome | Tipo | Faixa | Padrão |
  |---|---|---|---|---|
  | `0x2B` | `breathThresholdNegDbX100` | u16 | 0…9000 | 3550 |
  | `0x2C` | `breathMetric` | u8 | 0 RMS / 1 pico | 0 |
  | `0x2D` | `breathDetector` | u8 | 0 absoluto / 1 adaptativo | 0 |
  | `0x2E` | `breathRearmDb` | u8 | 0…20 | 3 |

  - `0x15` é aposentado e vira reservado; uma escrita com ele é rejeitada inteira.
  - `0x11`/`0x12` (alfas) passam a valer só para o adaptativo.
- **Potenciômetro:** ADC 0…4095 → −60…0 dBFS, linear, com histerese de 0,5 dB.
- **Detector:**
  - Absoluto: avaliado a cada bloco de 5 ms; dispara se **qualquer** microfone passa da linha.
  - Adaptativo: o algoritmo de hoje, a cada 3 blocos, sobre o maior dos dois microfones.
  - Rearme dos dois: o nível cai abaixo de `limiar × 10^(−rearmeDb/20)`.
  - Debounce: `debounceMs` (`0x13`, padrão 300).
- **Gráficos:**
  - Janela de `5 · 8 · 12 s`, padrão 12.
  - Faixas fixas do eixo Y: orientação −180…180; aceleração −4000…4000 mg; RMS e pico −120…0 dBFS; detecção de sopro −0,1…1,1; CO₂ 0…max(5000, `virtualPeakPpm`); potenciômetro 0…100 %; minilinha Taxa 0…1,2 × `imuNotifyHz`; minilinha Perda 0…100 %.
  - Rodapé: "Últimos N s, com todos os pacotes recebidos. Os gráficos são redesenhados 10 vezes por segundo; isso não atrasa o envio ao Pd nem o site."
- **Ponte com o Pd:**
  - Porta padrão 9478; repasse só dos tipos `0x01`, `0x02` e `0x07`; descarte quando `bufferedAmount > 4096`; reconexão a cada 2000 ms.
  - **Sem lista de origens.** O `[gaiaball]` aceita conexão de qualquer página: o domínio final do projeto ainda não foi definido (decisão do usuário). A proteção fica por conta do `bind` só em `127.0.0.1` e do pedido de permissão "Apps neste dispositivo" do Chrome. Nenhum domínio fica gravado no firmware nem no objeto.
  - Desconexão = WebSocket fechado ou 2000 ms sem pacotes.
  - Config-app publicada, provisoriamente, em `https://gaiasenses.github.io/gaiaball/`. O endereço aparece só como link no patch de ajuda e nos READMEs; quando o domínio final existir, troca-se só nesses lugares.
- **Textos verbatim do spec**, em pt-BR:
  - descrições dos detectores absoluto e adaptativo;
  - textos dos modos palco e ajuste;
  - rótulos `Absoluto ⟷ Adaptativo`, `Potenciômetro ⟷ Valor fixo`, `RMS ⟷ Pico` e `Modo ajuste ⟷ Modo palco`.
- **Regras do usuário:**
  - nunca escrever "Claude" em código, documentação, commit ou PR;
  - **sem** trailer `Co-Authored-By`;
  - texto de PR em 1ª pessoa, como Nycholas;
  - nada de estimativa de tempo de esforço na documentação;
  - não tocar em `firmware-original.ino` nem em `relatorio.md`;
  - não mudar o `git config`;
  - `git push` e PR só com OK explícito do usuário.
- **No `Gaiasenses-web`:**
  - rodar `graphify query "<pergunta>"` antes de buscar ou ler código, e `graphify update .` depois de editar código;
  - adicionar arquivos ao commit pelo caminho;
  - nunca commitar `docs/guia-api-publica.md` nem `graphify-out/`.
- **Comandos:**
  - firmware: `pio test -d firmware -e native` (raiz do `gaiaball`) e `pio run -d firmware -e esp32dev`;
  - config-app: `npm test` e `npm run build` (em `config-app/`); `node e2e-smoke.mjs` depois do build;
  - site: `npm test`, `npx tsc --noEmit`, `npm run lint`, `npm run patches:check`, `npm run build`.

## Foco de revisão

1. **Bola com firmware antigo**, mandando o pacote de movimento v1. A config-app mostra o aviso de firmware desatualizado e nada é decodificado errado; o `[gaiaball]` e o site descartam o pacote sem travar. Testes nas Tarefas 2, 3 e 10.
2. **Pd fechado, reaberto no meio da apresentação, ou permissão do Chrome negada.** A ponte tenta de novo a cada 2 s, sem lançar exceção; o status é correto; os pacotes são descartados, nunca acumulados. Testes na Tarefa 9.
3. **Segunda aba, reconexão em sequência ou quadro malformado chegando ao `[gaiaball]`.** A conexão nova substitui a antiga, sem vazar socket e sem travar o Pd. Testes nas Tarefas 11 e 12.
4. **Sopro longo, sala barulhenta e desconexão no meio de um sopro.** Um disparo por sopro; o bit aparece em pelo menos um pacote; na desconexão, `gaia.breath` vai a 0. Testes nas Tarefas 4, 6 e 12.
5. **Janela do Chrome escondida atrás do Pd.** O repasse não depende de `document.hidden` nem de temporizador. Teste na Tarefa 9 e medição na bancada.

---

## Tarefa 0: Copiar este plano para o repositório

- [ ] Copiar este arquivo para `gaiaball/docs/superpowers/plans/2026-11-14-pd-via-config-app.md`, na branch `feat/pd-via-config-app`.
- [ ] Atualizar o spec (`docs/superpowers/specs/2026-11-14-pd-via-config-app-design.md`, Parte 0) com a decisão do áudio em u8 meio-dB:
  - tabela de bytes com 27 B: áudio nos offsets 23–26;
  - "Sem custo no rádio" vira "−32 µs por pacote";
  - entra na lista de decisões do usuário.
- [ ] Atualizar o spec com o `0x2B` sem sinal (ajuste 1 deste plano).
- [ ] Atualizar o spec, Parte 2, com o ajuste 6: sai a lista de origens do `[gaiaball]` e a decisão nº 11 passa a ser "aceita qualquer origem; um cliente por vez".
- [ ] Commit: `docs(plan): implementation plan for the Pd bridge, mg acceleration, breath detector and stage mode`.

---

## Parte 0 — Aceleração em mg (pacote de movimento v2)

### Tarefa 1: Firmware — pacote de movimento v2 (aceleração em mg, áudio em meio-dB, 27 B)

**Arquivos:**
- Modificar `firmware/Gaiaball/src/logic/Packets.h`:
  - `:9`: manter `kProtocolVersion = 1` e acrescentar `constexpr uint8_t kImuPacketVersion = 2;`;
  - struct `ImuPacket` (`:71-89`):
    - `:76`: aceleração "mg (1 g = 1000 mg)";
    - `int16_t audioRmsDbX100[2]` e `audioPeakDbX100[2]` viram `uint8_t audioRmsHalfDb[2]` e `uint8_t audioPeakHalfDb[2]`;
    - o comentário de `:78-86` passa a dizer 27 B / 732 µs;
  - `static_assert(sizeof(ImuPacket) == 27)` (`:185`).
- Modificar `firmware/Gaiaball/src/logic/PacketCodec.{h,cpp}`: `makeImuPacket` (`.cpp:13-40`) escreve `kImuPacketVersion`, converte a aceleração em `:31-33` e codifica o áudio em `:35-38` com `dbX100ToHalfDb(rmsToDbfsX100(…))` e `dbX100ToHalfDb(peakToDbfsX100(…))`.
- Modificar `test_protocol.cpp:9-12`: o `static_assert` do Imu passa a 27.
- Atualizar no `firmware/README.md` a tabela de orçamento do rádio, que muda com o IMU de 27 B. Pela fórmula `(P + 17) × 8 + 380` µs, o IMU dá 732 µs por pacote. Recalcular:
  - a linha do ImuPacket (`:248`): 27 B, 1350 B/s, 36,6 ms/s a 50 Hz;
  - os totais de `:229-241`, por exemplo o padrão v9: 2611 B/s e 76,1 ms/s;
  - os cenários de 100 e 200 Hz, pela mesma fórmula;
  - o MTU do IMU: ≥ 30.
- Modificar os comentários "FIXED by the wire protocol" em `services/ImuDriver.h:27-30`, `services/Mpu9250.h:14`, `services/Icm20948.h:15` e `services/ImuService.h:32`. Novo texto: a aceleração vai em mg; o ±4 g deixa de ser fixado pelo protocolo.
- Testar em `firmware/test/test_protocol/test_protocol.cpp` (golden em `:48-67`; silêncio em `:89-98`).
- Documentação, no mesmo commit:
  - `firmware/docs/protocol.md`: escalas `:48` (aceleração) e `:53-56` (microfones em meio-dB); seção ImuPacket `:67-100` com `version = 2`, 27 B e o novo mapa de bytes; regra de versionamento `:162-167` (primeiro caso real de versão nova); golden `:535-540`;
  - `firmware/README.md:334`.

**Interfaces:**
- Produz `int16_t accelCountsToMg(int16_t counts);`, declarado em `PacketCodec.h` (`lroundf(counts * 1000.0f / 8192.0f)`).
- Produz `uint8_t dbX100ToHalfDb(int16_t dbX100);`, declarado em `AudioFeatures.h` (`clamp(lround((dbX100 + 12000) / 50.0), 0, 255)`; o piso −12000 vira 0).
- Produz o `makeImuPacket` com assinatura **inalterada**: continua recebendo contagens.
- Produz `kImuPacketVersion`.

- [ ] **Passo 1: testes que falham**, em `test_protocol.cpp`:
  - `test_imu_encode_golden`: `makeImuPacket(258, 0x0A0B0C0Du, {1,0,0,0}, {8192, -4096, 1234}, 0b1011, lv)`, com o mesmo `lv` de hoje (RMS 0 e −20 dBFS, pico 0 e −6,02 dBFS), igual aos 27 bytes
    `02 01 02 01 0D 0C 0B 0A 00 40 00 00 00 00 00 00 E8 03 0C FE 97 00 0B F0 C8 F0 E4`.
    O áudio fica 240 = 0 dB, 200 = −20 dB, 240 = 0 dB e 228 = −6,0 dB.
  - `test_db_to_half_db`: `-12000→0`, `0→240`, `-2000→200`, `-602→228`, `750→255`, `6000→255`, `-12100→0`, `-3525→170` (−35,25 arredonda para −35,0).
  - `test_accel_counts_to_mg`: `0→0`, `4→0`, `5→1`, `-5→-1`, `8192→1000`, `-4096→-500`, `1234→151`, `32767→4000`, `-32768→-4000`.
  - `test_accel_neighbours_stay_distinct_in_range`: para `c` de −32768 a 32767, `accelCountsToMg(c+1) - accelCountsToMg(c)` está em {0, 1}. É monotônico; não perde a ordem.
  - `test_imu_encodes_silence_as_the_db_floor`: o byte 0 passa a esperar `0x02`, e os quatro bytes de áudio (23–26) valem `0x00`.
- [ ] **Passo 2:** `pio test -d firmware -e native -f test_protocol`. Esperado: FAIL (byte 0 = 01; aceleração em contagens).
- [ ] **Passo 3:** implementar `accelCountsToMg` e usar em `makeImuPacket`; escrever `kImuPacketVersion` no byte 0.
- [ ] **Passo 4:** `pio test -d firmware -e native`. Esperado: tudo PASS.
- [ ] **Passo 5:** atualizar os comentários e a documentação listados acima.
- [ ] **Passo 6:** commit `feat(fw): IMU packet v2 carries acceleration in milli-g and half-dB audio levels`.

### Tarefa 2: Config-app — ler o pacote de movimento v2 em mg

**Arquivos:**
- Modificar `config-app/src/protocol/packets.ts`:
  - acrescentar `IMU_PACKET_VERSION = 2` perto de `:4`;
  - `header()` (`:199-201`) ganha o parâmetro `version = PROTOCOL_VERSION`;
  - `IMU_PACKET_SIZE` (`:13`): 31 → 27;
  - `decodeImuPacket` (`:203-221`) passa `IMU_PACKET_VERSION`; o áudio passa a ser lido com `getUint8(23..26) / 2 − 120`;
  - `ImuSample.acc` (`:68-87`) passa a ser documentado em mg;
  - remover `ACCEL_LSB_PER_G` (`:27`).
- Modificar `config-app/src/panels/sensors-panel.ts`:
  - `:340-342`: sai a divisão por `ACCEL_LSB_PER_G`;
  - `rowTitles` (`:232`): "Aceleração (mg)";
  - traços `:207-209`: "acc X (mg)" etc.
- Modificar `config-app/src/main.ts`:
  - o demo do IMU (`:184`) gera aceleração em mg (repouso ≈ `[0, 0, 1000]`);
  - mostrar um aviso no `#banner` (`:23`) quando o cliente acusar firmware antigo.
- Modificar `config-app/src/ble/gaiaball-client.ts`, `subscribe` (`:176-190`): ao receber um pacote na característica do IMU com `byte0 === 1 && byte1 === PKT_IMU`, despachar `'firmware-outdated'` uma vez por conexão.
- Testar em `config-app/src/protocol/packets.test.ts` (golden IMU `:18-44`; casos inválidos `:115-150`).

**Interfaces:**
- Produz `IMU_PACKET_VERSION`.
- Produz `isLegacyImuPacket(dv: DataView): boolean`, exportado de `packets.ts`.
- Produz o evento `'firmware-outdated'` no `GaiaballClient`.

- [ ] **Passo 1: testes que falham:**
  - golden v2 de 27 B (o hex da Tarefa 1) → `acc` = `[1000, -500, 151]`, `flags` = 0x0B, `audioRmsDbfs` = `[0, -20]`, `audioPeakDbfs` = `[0, -6]`;
  - o golden antigo de 31 B, com `01` no byte 0 → `null`;
  - `isLegacyImuPacket(hex(golden antigo)) === true` e `isLegacyImuPacket(hex(golden v2)) === false`;
  - o caso de silêncio (`:35-44`) vira um pacote v2 de 27 B com o áudio todo `00` → os quatro valores = −120.
- [ ] **Passo 2:** `npm test` (em `config-app/`). Esperado: FAIL.
- [ ] **Passo 3:** implementar.
- [ ] **Passo 4:** `npm test && npm run build`. Esperado: PASS e build ok.
- [ ] **Passo 5:** commit `feat(app): decode IMU packet v2 (mg, half-dB audio) and flag outdated firmware`.

### Tarefa 3: Site — ler o v2, vocabulário e documentação em mg

**Repo:** `Gaiasenses-web`, branch nova `feat/gaiaball-mg` a partir de `feat/gaiaball-navegacao`. Rodar `graphify query` antes de ler.

**Arquivos:**
- Modificar `app/[locale]/map3/ble-decode.ts`:
  - novo `IMU_PACKET_VERSION = 2`, usado só na checagem do IMU (`:164-170`);
  - `IMU_PACKET_MIN_SIZE` (`:99`): 31 → 27. O comentário de `:78-98` ganha o caso 31 → 27 da versão 2; a regra continua sendo `≥ mínimo`;
  - documentar `acc` em mg no tipo `espResponse` (`:28-32`); a leitura `getInt16` (`:183-187`) não muda;
  - envelope de áudio (`:195-200`): `getUint8(23)`, `(24)`, `(25)`, `(26)`, cada um `/ 2 − 120`.
- Testar em `tests/ble-decode.test.ts`:
  - `GOLDEN_IMU` (`:18-20`) ← hex v2 de 27 B da Tarefa 1;
  - o construtor `imuPacket()` (`:47-77`) escreve a versão 2, 27 B, com silêncio = bytes `00`;
  - asserções de aceleração em `:89-97` e `:176-187` → `{ x: 1000, y: -500, z: 151 }`;
  - áudio `rmsDb` = `[0, -20]` e `peakDb` = `[0, -6]`;
  - o caso "futuro" passa a ter 44 B (27 + 17).
- `tests/gaiaball-live.test.ts`: os valores de aceleração são arbitrários; só ajustar comentários, se houver.
- Modificar `lib/gaia-vocabulary.json`: `gaia.acc.x/y/z` (`:38-70`) com `"unit": "mg"`, `"range": [-4000, 4000]` e ajuda dizendo "em mg (1000 = 1 g)"; `gaia.sensors` (`:167-176`) com "aceX aceY aceZ em mg".
- Regenerar `docs/musico/vocabulario.md` com `node scripts/gen-musician-docs.mjs` (o `patches:check` confere).
- Modificar `app/[locale]/map3/pd4web-patch-log.tsx`:
  - `ACC_EPSILON_MAX = 100` → `1000` (`:56`);
  - rótulo `accEpsilon` (`:248-264`) → "accEpsilon (mg; também vale para o CO₂, em ppm)". Esse épsilon também é a zona morta do CO₂: `gaiasenses-map.tsx:655,668`.
- Documentação:
  - `lib/gaiaball-live.ts` (comentário `:1-27`): `acc` em mg;
  - `app/[locale]/map3/pd-send-rule.ts:6-9`: a lista com a aceleração em mg;
  - `README.md`: `:399` (API das sketches), `:505` (contrato do Pd), `:594` ("accel is raw DMP counts" → mg; versão 2; "≥ 31 B" → "≥ 27 B"; envelope em meio-dB);
  - `docs/musico/README.md`: tabela `:137-139` (−4000 a 4000 mg); nota do `paraisoGaia43` em `:162-186`, reescrita com a migração para mg do spec: `[/ 8000]`→`[/ 1000]`, `[> 6000]`/`[< -6000]`→`[> 732]`/`[< -732]`, `[/ 7000]`→`[/ 854.5]`, `[/ 11000]`→`[/ 1343]`, `[+ 11000]`+`[/ 22000]`→`[+ 1343]`+`[/ 2686]`, o `accEpsilon` pode ficar em 0.05;
  - `docs/musico/manual.html:763-765`: −4000 a 4000 mg.
- **Não** mexer em `patches/paraisoGaia43/*`: quem edita é o Gabriel.

- [ ] **Passo 1:** testes que falham: golden v2 decodificado com `acc` em mg; pacote v1 → `null`.
- [ ] **Passo 2:** `npm test`. Esperado: FAIL.
- [ ] **Passo 3:** implementar o decodificador, o vocabulário, a regeneração e a documentação.
- [ ] **Passo 4:** `npm test && npx tsc --noEmit && npm run lint && npm run patches:check && npm run build`. Esperado: tudo ok.
- [ ] **Passo 5:** `graphify update .`
- [ ] **Passo 6:** commit, com os arquivos adicionados pelo caminho: `feat(ble): read Gaiaball IMU packet v2 (milli-g, half-dB audio)`.

---

## Parte 1 — Detector de sopro e gráficos

### Tarefa 4: Firmware — `BreathDetector` (lógica pura) e conversões dB→contagens

**Arquivos:**
- Criar `firmware/Gaiaball/src/logic/BreathDetector.{h,cpp}`.
- `firmware/Gaiaball/src/logic/PeakDetector.{h,cpp}` continua até a Tarefa 6, porque o `AudioService` ainda o usa. Os testes dele saem já aqui; o `BreathDetector` incorpora o modo adaptativo.
- Modificar `firmware/Gaiaball/src/logic/AudioFeatures.{h,cpp}` (as fórmulas inversas de `.cpp:9-21`).
- Testar em `firmware/test/test_audio/test_audio.cpp`:
  - substituir `test_peak_fires_and_debounces` `:70`, `test_hysteresis_rearm` `:88`, `test_baseline_adapts` `:101` e `test_debounce_wraparound` `:116`;
  - `main()` em `:244-247`;
  - corrigir o `step()` (`:63-68`) para 15 ms, a cadência real do adaptativo.

**Interfaces (produz):**
```cpp
enum class BreathMode : uint8_t { Absolute = 0, Adaptive = 1 };
struct BreathParams {
  BreathMode mode = BreathMode::Absolute;
  float thresholdCounts = 0.0f;  // absoluto: a linha; adaptativo: o quanto sobe acima da linha de base
  uint8_t rearmDb = 3;           // 0..20
  uint32_t debounceMs = 300;
  float fastAlpha = 0.25f, slowAlpha = 0.995f;  // só adaptativo
};
class BreathDetector {
 public:
  void configure(const BreathParams &p);              // zera o estado se o modo mudar
  bool update(const float level[2], uint32_t nowMs);  // true no disparo (borda)
  bool recognized() const;                            // 1 do disparo até o rearme
  float triggerLevel() const;                         // absoluto: thresholdCounts; adaptativo: slow + thresholdCounts
};
float dbfsToRmsCounts(float dbfs);   // 2^23/√2 · 10^(dbfs/20)  (AES17)
float dbfsToPeakCounts(float dbfs);  // 2^23 · 10^(dbfs/20)
float dbToAmplitudeRatio(float db);  // 10^(−db/20); 3 dB → 0,7079
```
- **Absoluto:** dispara se `(L > thr || R > thr)`, está armado e o debounce passou (com a mesma aritmética à prova de estouro do `PeakDetector.cpp:11-31`). Rearma quando `L < thr·ratio && R < thr·ratio`.
- **Adaptativo:** `level = max(L, R)`; os EMAs `fast`/`slow` iguais aos de hoje, semeados no primeiro `update`. Dispara se `fast − slow > thr`. Rearma quando `fast − slow < thr·ratio`.

- [ ] **Passo 1: testes que falham:**
  - `test_abs_fires_on_left` / `test_abs_fires_on_right`: `thr=1000`, um nível `{1200, 0}` (ou `{0, 1200}`) → `update` devolve true;
  - `test_abs_rearm_at_configured_db`: `rearmDb=3`; depois do disparo, `{800, 800}` continua desarmado; `{700, 700}` rearma (700 < 707,9);
  - `test_abs_one_fire_per_long_breath`: 100 atualizações seguidas acima da linha → um só true; `recognized()` fica true o tempo todo;
  - `test_abs_debounce`: rearma e passa de novo antes de 300 ms → sem disparo; depois de 300 ms → dispara;
  - `test_rearm_zero_db`: `rearmDb=0` → rearma assim que cai abaixo de `thr`;
  - `test_adaptive_fires_and_debounces`, `test_adaptive_rearm_db`, `test_adaptive_baseline_adapts`, `test_debounce_wraparound`: portados dos quatro testes de hoje, com `rearmDb=3` (razão 0,7079 em vez de 0,7);
  - `test_trigger_level`: absoluto → `thresholdCounts`; adaptativo depois de semeado com 5000 → `5000 + thr`;
  - `test_dbfs_to_counts`: `dbfsToRmsCounts(-35.5f)` ≈ 99580 (±0,5 %); `dbfsToPeakCounts(0)` = 8388608; `dbfsToRmsCounts(0)` ≈ 5931642; `dbToAmplitudeRatio(3)` ≈ 0,70795.
- [ ] **Passo 2:** `pio test -d firmware -e native -f test_audio`. Esperado: FAIL (arquivos inexistentes).
- [ ] **Passo 3:** implementar o `BreathDetector` e os conversores. O `PeakDetector` só é apagado na Tarefa 6.
- [ ] **Passo 4:** `pio test -d firmware -e native -f test_audio`. Esperado: PASS.
- [ ] **Passo 5:** commit `feat(fw): breath detector with absolute and adaptive modes and dB rearm`.

### Tarefa 5: Firmware — TLVs novos, aposentar `0x15` e potenciômetro em dB

**Arquivos:**
- Modificar `firmware/Gaiaball/src/logic/GaiaConfig.h`:
  - remover `thresholdFixed` (`:75`);
  - acrescentar `uint16_t breathThresholdNegDbX100 = 3550; uint8_t breathMetric = 0; uint8_t breathDetector = 0; uint8_t breathRearmDb = 3;`.
- Modificar `firmware/Gaiaball/src/logic/TlvCodec.cpp`:
  - `kRegs` (`:25-61`): remover a linha de `0x15` (`:38`); acrescentar depois de `0x2A` `{0x2B, Kind::U16, 0, 9000}, {0x2C, Kind::U8, 0, 1}, {0x2D, Kind::U8, 0, 1}, {0x2E, Kind::U8, 0, 20}`;
  - `numField` (`:75-134`): remover o caso `0x15`; acrescentar os quatro novos;
  - comentário dos reservados (`:62-66`): incluir `0x15` (era `thresholdFixed`).
- Modificar `firmware/Gaiaball/src/services/ConfigStore.h:16`: `kConfigSchemaVersion = 8` (v8: detector de sopro em dB; `0x15` aposentado).
- Criar `firmware/Gaiaball/src/logic/PotMapping.h` (lógica pura, testável).
- Testar em `firmware/test/test_config/test_config.cpp`; criar `firmware/test/test_pot/test_pot.cpp`.

**Interfaces (produz):**
```cpp
constexpr float kPotDbMin = -60.0f, kPotDbMax = 0.0f, kPotDbHysteresis = 0.5f;
float potRawToDbfs(float filteredRaw);           // 0..4095 → −60..0, linear, com clamp
bool potDbChanged(float newDb, float currentDb); // |Δ| ≥ 0,5
```

- [ ] **Passo 1: testes que falham:**
  - em `test_config`:
    - `test_breath_regs_accept_range`: `0x2B` 0/9000, `0x2C` 0/1, `0x2D` 0/1 e `0x2E` 0/20 são aplicados;
    - `test_breath_regs_reject_out_of_range`: `0x2B` 9001, `0x2C` 2, `0x2D` 2 e `0x2E` 21 → `assertRejected`;
    - `test_reserved_ids_rejected` (`:154-163`): acrescentar `{0x15, 4, …}`;
    - `test_lenient_load_skips_retired_0x15`: um registro `0x15` no dump é pulado, e os demais são aplicados;
    - padrões novos no `test_defaults` existente;
    - a serialização contém `0x2B`…`0x2E` e não contém `0x15`;
  - em `test_pot`: `potRawToDbfs(0)=-60`, `(4095)=0`, `(2047.5)=-30`, `(-5)=-60`, `(5000)=0`; `potDbChanged(-30.4,-30)` false, `potDbChanged(-30.5,-30)` true.
- [ ] **Passo 2:** `pio test -d firmware -e native -f test_config -f test_pot`. Esperado: FAIL.
- [ ] **Passo 3:** implementar.
- [ ] **Passo 4:** `pio test -d firmware -e native`. Esperado: PASS. (O `test_config` pode referenciar `thresholdFixed`: trocar.)
- [ ] **Passo 5:** commit `feat(fw): breath TLVs 0x2B-0x2E, retire 0x15, potentiometer in dB`.

### Tarefa 6: Firmware — ligar detector, bits e potenciômetro no app

**Arquivos:**
- Modificar `services/AudioService.{h,cpp}`:
  - `detector_` (`.h:102`) vira `BreathDetector`;
  - a configuração vem de `applyConfig` (`.cpp:74-86`) e da reconfiguração na task de áudio (`:134-136`): modo, medida, rearme, debounce e alfas;
  - em `:195-208`: a entrada é `lv.rms` ou `lv.peak`, conforme `breathMetric`; o portão de fase (`:203-204`) só vale para o adaptativo (`kAdaptiveBlocks = 3`); o absoluto avalia todo bloco;
  - o provedor (`:205`) passa a devolver **dBFS**, convertido na task de áudio com `dbfsToRmsCounts`/`dbfsToPeakCounts`.
- Modificar `services/PotService.{h,cpp}`:
  - o mapeamento (`.cpp:23-32`) passa a usar `potRawToDbfs` e `potDbChanged`;
  - `threshold()` (`.h:24`) vira `float thresholdDbfs() const`: o potenciômetro ou `-breathThresholdNegDbX100 / 100`;
  - saem `kThresholdMin`/`Max` (`.h:18-19`).
- Modificar `App.{h,cpp}`:
  - `threshold_` (`App.h:86`) passa a guardar dBFS, com padrão −35,5;
  - em `handleImuSample` (`:182-186`): `if (audio_.breathRecognized() | audio_.takeBreathLatch()) flags |= kImuBreath;`;
  - em `sendSensors` (`:306-317`): `threshold = audio_.triggerLevelCounts()`; flags `kSensMetricPeak`/`kSensAdaptive`.
- Modificar `logic/Packets.h`: `kImuBreath = 1u << 4`, `kSensMetricPeak = 1u << 4`, `kSensAdaptive = 1u << 5`; atualizar o comentário do `SensorsPacket` (`:139-142`).
- Testar em `test_protocol.cpp`.
- Documentação no mesmo commit:
  - `firmware/docs/protocol.md`: flags do ImuPacket `:88`; SensorsPacket `:307-346` (sentido do limiar e bits 4 e 5); tabela de TLVs `:430-441` (`0x11`/`0x12` só no adaptativo, `0x15` aposentado, linhas `0x2B`–`0x2E` depois de `:454`); reservados `:484-498`; goldens `:531-553`;
  - `firmware/README.md`: linha do detector `:369`; seção do potenciômetro `:395-407`; referência rápida `:461`, `:464`; tabela de Flash/RAM `:76-79` com o build novo;
  - `docs/bench-validation.md`: roteiro do sopro, cobrindo os dois detectores, RMS e pico, potenciômetro e valor fixo, sala silenciosa e barulhenta.

**Interfaces:**
- Consome a Tarefa 4 (`BreathDetector`, conversores) e a Tarefa 5 (campos e `PotMapping`).
- Produz:
  - `AudioService::breathRecognized() const -> bool` (atômico);
  - `AudioService::takeBreathLatch() -> bool` (`exchange(false)`; a task de áudio faz `store(true)` a cada bloco com `recognized()`);
  - `AudioService::triggerLevelCounts() const -> float` (atômico).

- [ ] **Passo 1: testes que falham** em `test_protocol`:
  - `test_imu_encode_golden_breath`: o mesmo do golden v2 com `flags = 0b11011` →
    `02 01 02 01 0D 0C 0B 0A 00 40 00 00 00 00 00 00 E8 03 0C FE 97 00 1B F0 C8 F0 E4`;
  - `test_sensors_encode_golden_bits`: `makeSensorsPacket(7, 123456, 12345, 23456, 100000, 2048, 0b110111)` →
    `01 04 07 00 40 E2 01 00 39 30 00 00 A0 5B 00 00 A0 86 01 00 00 08 37`;
  - `kImuBreath == 0x10`, `kSensMetricPeak == 0x10`, `kSensAdaptive == 0x20`.
- [ ] **Passo 2:** `pio test -d firmware -e native -f test_protocol`. Esperado: FAIL (constantes inexistentes).
- [ ] **Passo 3:** implementar o app, o áudio e o potenciômetro; apagar `PeakDetector.{h,cpp}`.
- [ ] **Passo 4:** `pio test -d firmware -e native` → PASS; depois `pio run -d firmware -e esp32dev` → SUCCESS. Anotar Flash e RAM no README.
- [ ] **Passo 5:** commit `feat(fw): wire the breath detector, breath bit and dB threshold into the app`.

### Tarefa 7: Config-app — registradores, decodificação dos bits, chave deslizante e campos do sopro

**Arquivos:**
- Modificar `config-app/src/protocol/tlv.ts`:
  - mapa `TLV` (`:4-68`): remover `thresholdFixed: 0x15` (`:28`) e pôr comentário de reservado; acrescentar `breathThresholdNegDbX100: 0x2b`, `breathMetric: 0x2c`, `breathDetector: 0x2d`, `breathRearmDb: 0x2e`;
  - `SPECS` (`:89-149`): `U16` para o primeiro, `U8` para os outros.
- Modificar `config-app/src/protocol/packets.ts`:
  - `SensorsSample` (`:168-184`) ganha `metricPeak` e `adaptive` (bits 4 e 5);
  - `ImuSample` ganha `breath` (bit 4);
  - novos `peakToDbfs(peak)` e `thresholdToDbfs(s: SensorsSample)`, que escolhe a fórmula pela medida.
- Modificar `config-app/src/panels/config-panel.ts`:
  - `Kind` (`:6`) ganha `'switch'`;
  - `Field` (`:7-28`) ganha `switchOptions?: readonly [{value:number;label:string},{value:number;label:string}]`, `describe?: (v:number)=>string`, `negate?: boolean`, `step?: number` e `enabled?: (cfg)=>boolean`;
  - renderização em `buildGroup` (`:455-526`); `getValue`/`setValue` (`:546-580`) tratam `switch` e `negate`;
  - o ouvinte de `change` (`:534-541`) recalcula a visibilidade também ao mudar `breathDetector`, e o estado habilitado ao mudar `potControlsThreshold`;
  - grupo de áudio (`:147-160`), campos em ordem:

    | Campo | Tipo | Detalhes |
    |---|---|---|
    | `breathDetector` | switch | `[{0,'Absoluto'},{1,'Adaptativo'}]`; `describe` com os dois textos verbatim do spec |
    | `potControlsThreshold` | switch | `[{1,'Potenciômetro'},{0,'Valor fixo'}]` (substitui o checkbox `:155`) |
    | `breathMetric` | switch | `[{0,'RMS'},{1,'Pico'}]` |
    | `breathThresholdNegDbX100` | number | "Sensibilidade (dBFS)"; `scale: 100`, `negate: true`, `step: 0.5`, `min 0`, `max 9000`; `enabled` só com `potControlsThreshold === 0`; mostra ao lado o dBFS atual do potenciômetro (vindo de `'sensors'`) |
    | `breathRearmDb` | number | "Rearme (dB)", 0…20, `step 1` |
    | `debounceMs` | number | inalterado |
    | `audioFastAlphaX1000` / `audioSlowAlphaX1000` | number | `visible` só com `breathDetector === 1` |

  - cada campo novo ganha um `info:` didático, no padrão dos existentes: o que faz e a unidade. O da sensibilidade explica o sentido em cada detector: a linha, no absoluto; o quanto acima do ruído, no adaptativo;
  - remover o campo `thresholdFixed` (`:157`);
  - `BASIC_IDS` (`:243-251`): sai `thresholdFixed`; entram `breathDetector`, `breathMetric`, `breathThresholdNegDbX100` e `breathRearmDb` (total 23);
  - `FIELD_DEFAULTS` (`:262-317`): sai `0x15`; entram os padrões novos.
- Modificar `config-app/src/style.css`: classe `.switch`, com os tokens `--accent`, `--line`, `--raised`, `--text`, `--focus` (`:6-43`), foco visível, temas claro e escuro.
- Testar:
  - `config-app/src/protocol/packets.test.ts`: a contagem de chaves `TLV` (`:154`) vai de 60 para 63; os bits do Sensors; `peakToDbfs`;
  - `config-app/src/panels/config-panel.test.ts`: o teto do Básico (`:48`) vai a 23, com comentário: "+4 campos do detector de sopro, −1 limiar fixo em contagens".
- Também: `grep -rn "thresholdFixed" config-app/src` não deve sobrar nada.

**Interfaces (produz):**
- `peakToDbfs(peak: number): number`;
- `thresholdToDbfs(s: SensorsSample): number`;
- `SensorsSample.metricPeak`, `SensorsSample.adaptive`, `ImuSample.breath`;
- `Field.kind === 'switch'`, que a Tarefa 14 reutiliza no cabeçalho;
- a função `renderSwitch(opts): HTMLElement`, exportada de um módulo novo `src/ui/switch.ts`, com `opts = { options, value, onChange, label }`. O `config-panel` e o modo palco usam essa mesma função.

- [ ] **Passo 1: testes que falham:**
  - `peakToDbfs(8388608) === 0`; `peakToDbfs(4194304)` ≈ −6,02;
  - `thresholdToDbfs` usa a fórmula de pico quando `metricPeak`;
  - o hex do Sensors com flags `0x37` → `metricPeak` e `adaptive` true;
  - os padrões dos campos novos passam por `encodeTlv`;
  - `negate`: 3550 ↔ −35,5;
  - a visibilidade dos alfas segue `breathDetector`;
  - `BASIC_FIELD_IDS.size === 23`.
- [ ] **Passo 2:** `npm test`. Esperado: FAIL.
- [ ] **Passo 3:** implementar.
- [ ] **Passo 4:** `npm test && npm run build`. Esperado: PASS.
- [ ] **Passo 5:** commit `feat(app): breath detector controls, slide switch and dB sensitivity`.

### Tarefa 8: Config-app — gráficos com todos os pacotes, janela, faixas fixas e pulso de sopro

**Arquivos:**
- Criar `config-app/src/panels/chart-model.ts`: lógica pura, sem importar `theme.ts` (ele toca `window` ao carregar, `theme.ts:7`).
- Modificar `config-app/src/panels/sensors-panel.ts`:
  - fila por pacote no lugar do `latest` (`:74-78`, `:103`);
  - `tick` (`:322-360`) acrescenta todos os pontos da fila, cada um com o horário de chegada;
  - `MAX_POINTS = 12 × 200`, para a maior janela à maior taxa;
  - o eixo X mostra `[t−N, t]`, com um redesenho por tick: atualizar `layout.xaxis8.range` antes do `extendTraces`, porque todos os eixos X seguem o último;
  - linhas novas: `N_ROWS = 8`, nesta ordem: Orientação, Aceleração (mg), Mic esquerdo, Mic direito, Pico por canal, **Detecção de sopro**, CO₂, Potenciômetro;
  - limiar em linha contínua (sai o `dash 'dot'` de `:212`, `:214`) e um traço de limiar no "Pico por canal";
  - na medida RMS, o limiar aparece nas linhas dos mics e é `null` no pico; na medida pico, o inverso;
  - o valor vem de `thresholdToDbfs`;
  - seletor `5 s · 8 s · 12 s` acima dos gráficos;
  - faixas do eixo Y fixas desde a criação, com `autorange: false` e duplo clique voltando à faixa;
  - rodapé (`:98`) com o texto verbatim;
  - cartão do potenciômetro (`:92`, `:311-313`): "limiar X dBFS (RMS|pico, potenciômetro|fixo)";
  - `traceColors` (`:67-72`) acompanha a nova ordem.
- Modificar `config-app/src/panels/sparkline.ts:13-30`: `drawSparkline(canvas, values, color, range?: readonly [number, number])`.
- Modificar `config-app/src/panels/metrics-panel.ts:88-139`: usa `sparkRange`; a taxa configurada vem do evento `'config'` (TLV `imuNotifyHz` 0x02).
- Modificar `config-app/src/main.ts`, demo (`:184`, `:212`): gerar pulsos de sopro e os bits do Sensors.
- Testar em `config-app/src/panels/chart-model.test.ts`.

**Interfaces (produz):**
```ts
export const WINDOW_OPTIONS_S = [5, 8, 12] as const;
export type WindowS = (typeof WINDOW_OPTIONS_S)[number];
export const DEFAULT_WINDOW_S: WindowS = 12;
export const WINDOW_STORAGE_KEY = 'gaiaball-chart-window';
export const Y_RANGES = { orientation: [-180, 180], accel: [-4000, 4000], micRms: [-120, 0],
  micPeak: [-120, 0], breath: [-0.1, 1.1], pot: [0, 100] } as const;
export function co2Range(virtualPeakPpm: number): [number, number];    // [0, max(5000, v)]
export function sparkRange(kind: 'rate' | 'loss', configuredHz: number): [number, number];
export function loadWindowS(storage: Pick<Storage, 'getItem'> | null): WindowS;  // inválido → 12
export function saveWindowS(storage: Pick<Storage, 'setItem'> | null, w: WindowS): void;
export class SampleQueue<T> { push(t: number, v: T): void; drain(): { t: number; v: T }[]; clear(): void }
export function thresholdRows(metricPeak: boolean): { rms: boolean; peak: boolean };
```

- [ ] **Passo 1: testes que falham:**
  - a fila guarda os cinco pacotes que chegaram entre dois ticks, e `drain` a esvazia;
  - `loadWindowS` devolve 12 para `null`, `'7'` ou erro, e 8 para `'8'`;
  - `co2Range(2100)` = `[0, 5000]`; `co2Range(8000)` = `[0, 8000]`;
  - `sparkRange('rate', 50)` = `[0, 60]`; `sparkRange('loss', 50)` = `[0, 100]`;
  - `thresholdRows(true)` = `{ rms: false, peak: true }`.
- [ ] **Passo 2:** `npm test`. Esperado: FAIL.
- [ ] **Passo 3:** implementar o modelo e o painel.
- [ ] **Passo 4:** `npm test && npm run build && node e2e-smoke.mjs`. No e2e, acrescentar às strings conferidas (`:15`) "Detecção de sopro" e "Últimos". Esperado: PASS.
- [ ] **Passo 5:** verificação visual com Playwright em `npm run dev`, `?demo=1`:
  - as faixas fixas aparecem desde o primeiro quadro;
  - 5, 8 e 12 s funcionam;
  - pulsos visíveis;
  - linha contínua na linha da medida ativa.

  Salvar as capturas fora do repositório.
- [ ] **Passo 6:** commit `feat(app): plot every packet, fixed axis ranges, time window and breath pulse chart`.

---

## Parte 2 — Ponte config-app → `[gaiaball]`

### Tarefa 9: Config-app — ingestão única, codificador binário e `PdBridge`

**Arquivos:**
- Criar `config-app/src/protocol/encode.ts`: `encodeImuPacket` (v2), `encodeCo2Packet`, `encodeNavPacket` e `encodeSensorsPacket`, com os mesmos layouts do decodificador.
- Modificar `config-app/src/ble/gaiaball-client.ts`:
  - novo `ingest(source, dv)`: chama primeiro `rawSink` (só para imu, co2 e nav), depois decodifica e despacha (a lógica atual de `subscribe`, `:176-190`). O aviso `'firmware-outdated'` da Tarefa 2 passa a sair do `ingest`, com o mesmo teste;
  - os ouvintes das características passam a chamar `ingest`;
  - guardar `imuChar`, `co2Char`, `navChar` e `diagChar` (hoje são locais em `connect()`, `:94-128`);
  - o tipo `BtCharacteristic` (`:25-30`) ganha `stopNotifications(): Promise<unknown>`;
  - `pauseDiag()`/`resumeDiag()`.
- Criar `config-app/src/pd/pd-bridge.ts`.
- Criar `config-app/src/pd/pd-panel.ts`: um bloco "Pd" dentro do painel Conexão, com a chave "Enviar ao Pd" (via `renderSwitch`), o campo da porta e a ajuda de "bloqueado". Mais o chip "Pd: …" no `#topbar` (`main.ts:25-34`).
- Modificar `config-app/src/main.ts`:
  - `client.setRawSink(b => bridge.forward(b))`;
  - o demo (`:184-293`) gera bytes com `encode.ts` e chama `client.ingest`;
  - o demo também mostra o status do Pd.
- Testar em `config-app/src/pd/pd-bridge.test.ts`, `config-app/src/protocol/encode.test.ts` e `config-app/src/ble/gaiaball-client.test.ts` (o `EventTarget` existe no Node).

**Interfaces (produz):**
```ts
// gaiaball-client.ts
type IngestSource = 'imu' | 'co2' | 'nav' | 'diag';
ingest(source: IngestSource, dv: DataView): void;
setRawSink(fn: ((bytes: Uint8Array) => void) | null): void;
pauseDiag(): Promise<void>; resumeDiag(): Promise<void>;
// pd-bridge.ts
export const PD_DEFAULT_PORT = 9478, PD_RETRY_MS = 2000, PD_MAX_BUFFERED = 4096;
export const PD_FORWARD_TYPES: readonly number[] = [0x01, 0x02, 0x07];
export const PD_PORT_KEY = 'gaiaball-pd-port', PD_ENABLED_KEY = 'gaiaball-pd-enabled';
export type PdState = 'off' | 'waiting' | 'connected' | 'blocked';
export interface PdStatus { state: PdState; rate: number; dropped: number }
export interface WsLike { readonly readyState: number; readonly bufferedAmount: number; binaryType: string;
  send(d: Uint8Array): void; close(): void;
  onopen: (() => void) | null; onclose: (() => void) | null; onerror: (() => void) | null }
export interface PdBridgeDeps { createSocket(url: string): WsLike; setTimeout: typeof setTimeout;
  clearTimeout: typeof clearTimeout; now(): number;
  queryLoopbackPermission(): Promise<'granted' | 'denied' | 'prompt' | 'unknown'> }
export class PdBridge extends EventTarget {   // despacha 'status' na mudança e 1×/s conectado
  constructor(deps: PdBridgeDeps);
  setEnabled(on: boolean): void; setPort(port: number): void;
  forward(bytes: Uint8Array): void;          // nunca lança exceção
  get status(): PdStatus;
}
```
- URL: `ws://127.0.0.1:${porta}`.
- `queryLoopbackPermission` em produção tenta `navigator.permissions.query({ name: 'loopback-network' })` e depois `'local-network-access'`. Qualquer erro devolve `'unknown'`.

- [ ] **Passo 1: testes que falham**, com `FakeSocket` e relógio falso:
  - repassa os tipos 0x01, 0x02 e 0x07; descarta 0x03, 0x04, 0x05, 0x06 e pacotes vazios;
  - com `bufferedAmount = 5000`, não chama `send` e `dropped` aumenta;
  - socket fechado → nova tentativa depois de 2000 ms, sem exceção; Pd reaberto → volta a repassar (Foco 2);
  - estados `waiting → connected → waiting`;
  - `queryLoopbackPermission` = `'denied'` → `blocked`;
  - `setPort(9500)` reconecta em `ws://127.0.0.1:9500`;
  - com `setEnabled(false)`, nenhum socket é criado;
  - `forward` não consulta `document` nem temporizador (Foco 5): funciona com `globalThis.document` indefinido;
  - cliente: `ingest('imu', dv)` chama `rawSink` **antes** do evento `'imu'` (a ordem é gravada num array);
  - um pacote de diagnóstico não vai ao `rawSink`;
  - `encode`: `encodeImuPacket` dos campos do golden v2 reproduz exatamente o hex da Tarefa 1; e `decode(encode(x))` devolve `x` para IMU, CO₂, Nav e Sensors.
- [ ] **Passo 2:** `npm test`. Esperado: FAIL.
- [ ] **Passo 3:** implementar.
- [ ] **Passo 4:** `npm test && npm run build`. Esperado: PASS.
- [ ] **Passo 5:** commit `feat(app): forward raw packets to Pd over a local WebSocket`.

### Tarefa 10: `[gaiaball]` — decodificador em C

**Arquivos:**
- Criar `pd-gaiaball/src/gb_decode.{h,c}`.
- Criar `pd-gaiaball/tests/test_decode.c`.
- Criar `pd-gaiaball/Makefile`. Nesta tarefa, só o alvo `test`: `cc -std=c99 -Wall -Wextra -Werror` e roda.

**Interfaces (produz):**
```c
typedef struct { uint16_t seq; uint32_t t_us; float quat[4]; int16_t acc_mg[3]; uint8_t flags;
                 int quat_valid, audio_valid, breath; float rms_db[2], peak_db[2]; } gb_imu;
typedef struct { uint16_t seq; uint32_t t_ms; uint16_t ppm; uint8_t flags, age_s; int valid, trigger; } gb_co2;
typedef struct { uint16_t seq; uint32_t t_us; uint8_t mode, flags; double lat, lon; float bearing, speed;
                 int valid, settled, moving; } gb_nav;
int gb_decode_imu(const uint8_t *p, size_t n, gb_imu *out);  /* 1 ok: version 2, type 0x01, n >= 27; áudio = u8/2 − 120 */
int gb_decode_co2(const uint8_t *p, size_t n, gb_co2 *out);  /* version 1, type 0x02, n >= 12 */
int gb_decode_nav(const uint8_t *p, size_t n, gb_nav *out);  /* version 1, type 0x07, n >= 22 */
void gb_euler_legacy(const float q[4], float *roll, float *pitch, float *yaw); /* = quatToEulerLegacy do site */
```
- Little-endian lido byte a byte (sem casts de struct).
- Escalas: Q14, dB ÷ 100, lat/lon ÷ 1e7, rumo ÷ 100, velocidade ÷ 10.

- [ ] **Passo 1: testes que falham:**
  - o golden v2 com sopro (Tarefa 6) → `acc_mg = {1000, -500, 151}`, `breath = 1`, `rms_db = {0, -20}`, `peak_db = {0, -6}`;
  - o golden v1 antigo → 0 (Foco 1);
  - 30 bytes → 0; um pacote de 48 bytes com o início igual ao golden → 1;
  - o golden de CO₂ (`protocol.md:542-546`) → `ppm = 1000`;
  - os goldens de navegação A e B (`protocol.md:290-305`, os mesmos de `packets.test.ts:209-249`) → os campos esperados nesses testes;
  - `gb_euler_legacy`: os casos da tabela do guia (`protocolo-ble.md:1675-1690`).
- [ ] **Passo 2:** `make -C pd-gaiaball test`. Esperado: FAIL (falha de compilação).
- [ ] **Passo 3:** implementar.
- [ ] **Passo 4:** `make -C pd-gaiaball test`. Esperado: PASS.
- [ ] **Passo 5:** commit `feat(pd): C decoder for the Gaiaball packets`.

### Tarefa 11: `[gaiaball]` — protocolo WebSocket, puro e testado

**Arquivos:**
- Criar `pd-gaiaball/src/gb_ws.{h,c}` (aperto de mão e quadros; sem checagem de origem).
- Criar `pd-gaiaball/src/gb_sha1.{h,c}` e `pd-gaiaball/src/gb_base64.{h,c}` (implementações pequenas, de domínio público).
- Criar `pd-gaiaball/tests/test_ws.c`.
- O `Makefile` (alvo `test`) passa a compilar os dois testes.

**Interfaces (produz):**
```c
#define GB_WS_MAX_PAYLOAD 1024
int gb_ws_accept_key(const char *client_key, char out[29]);
typedef enum { GB_HS_NEED_MORE, GB_HS_OK, GB_HS_BAD } gb_hs_result;
gb_hs_result gb_ws_parse_request(const char *buf, size_t n, char key[64]); /* exige GET, Upgrade: websocket, Sec-WebSocket-Version: 13 e Key; Origin é ignorado */
size_t gb_ws_response_101(const char accept[29], char *out, size_t cap);
size_t gb_ws_response_400(char *out, size_t cap);
typedef struct { uint8_t buf[2 * GB_WS_MAX_PAYLOAD + 16]; size_t len; } gb_ws_parser;
typedef enum { GB_WS_NONE, GB_WS_BINARY, GB_WS_PING, GB_WS_CLOSE, GB_WS_ERROR } gb_ws_kind;
void gb_ws_feed(gb_ws_parser *p, const uint8_t *data, size_t n);
gb_ws_kind gb_ws_next(gb_ws_parser *p, uint8_t *payload, size_t *len, uint16_t *close_code);
size_t gb_ws_frame(uint8_t opcode, const uint8_t *payload, size_t n, uint8_t *out, size_t cap); /* do servidor: sem máscara */
```
- Quadro sem máscara → `GB_WS_ERROR`, código 1002.
- Quadro de texto ou fragmentado → erro, código 1003.
- Payload acima de 1024 → erro, código 1009.

- [ ] **Passo 1: testes que falham:**
  - vetor da RFC 6455: `dGhlIHNhbXBsZSBub25jZQ==` → `s3pPLMBiTxaQ9kYGzzo2pMbHEm0=`;
  - pedidos: válido → `GB_HS_OK`; sem `Upgrade` → `BAD`; versão 8 → `BAD`; sem `Sec-WebSocket-Key` → `BAD`; cortado → `NEED_MORE`;
  - origem ignorada: o mesmo pedido válido com `Origin: https://qualquer.exemplo` ou sem `Origin` → `GB_HS_OK`;
  - quadros:
    - binário com máscara de 27 B;
    - dois quadros num só `feed`;
    - um quadro dividido em três `feed`;
    - sem máscara → 1002;
    - texto → 1003;
    - FIN = 0 → 1003;
    - 2000 B → 1009;
    - ping → `PING`;
    - close → `CLOSE`;
  - lixo aleatório: 10 000 entradas com semente fixa, nunca trava nem passa do buffer (Foco 3).
- [ ] **Passo 2:** `make -C pd-gaiaball test`. Esperado: FAIL.
- [ ] **Passo 3:** implementar.
- [ ] **Passo 4:** `make -C pd-gaiaball test`. Esperado: PASS (os dois testes).
- [ ] **Passo 5:** commit `feat(pd): minimal RFC 6455 server core`.

### Tarefa 12: `[gaiaball]` — objeto do Pd, patch de ajuda e substituição da ponte Python

**Arquivos:**
- Criar `pd-gaiaball/gaiaball.c`: a classe do Pd. Junta `gb_ws` e `gb_decode`.
- Criar `pd-gaiaball/gaiaball-help.pd`. Ele substitui o atual e contém:
  - um `[pd META]` com DESCRIPTION, VERSION e AUTHOR;
  - a tabela dos `[r gaia.*]` com faixa e unidade;
  - o botão `[pdcontrol]` com `browse https://gaiasenses.github.io/gaiaball/`;
  - o passo a passo em pt-BR.
- Apagar `pd-gaiaball/gaiaball.pd`, `pd-gaiaball/gaiaball_bridge.py` e `pd-gaiaball/requirements.txt`.
- `pd-lib-builder` entra como submódulo git em `pd-gaiaball/pd-lib-builder`.
- O `pd-gaiaball/Makefile` passa a ter `lib.name = gaiaball`, as fontes, `datafiles = gaiaball-help.pd README.md` e o alvo `test` da Tarefa 10.
- Criar `pd-gaiaball/tests/e2e/listen.pd` (imprime todos os `gaia.*`) e `pd-gaiaball/tests/e2e/send-goldens.mjs`: um cliente TCP em `node:net` que faz o aperto de mão com `Origin: http://localhost`, manda os goldens em quadros com máscara e testa uma segunda conexão.
- Reescrever `pd-gaiaball/README.md` para leigos:
  - instalar pelo Deken;
  - abrir a config-app;
  - criar `[gaiaball]`;
  - liberar o Chrome;
  - tabela de canais;
  - solução de problemas;
  - uma nota de segurança: o objeto escuta só no próprio computador e aceita qualquer página que o Chrome autorizar em "Apps neste dispositivo"; não libere essa permissão para sites desconhecidos;
  - listar o `PUBLICAR-NO-DEKEN.md`.
- Atualizar o `README.md` da raiz:
  - `:60-61` ("31 B" → "27 B", aceleração em mg, áudio em meio-dB, v2);
  - mapa do repositório `:77-89` (a linha `:83` passa de "ponte Python/bleak" para "external `[gaiaball]`");
  - seção de testes `:144-158` (acrescentar `make -C pd-gaiaball test`).

**Interfaces:**
- Consome as Tarefas 10 e 11.
- Comportamento exato:
  - `[gaiaball]` escuta em 9478; `[gaiaball 9500]` escuta em 9500. Faz `bind` só em `127.0.0.1`, com socket não bloqueante, e registra os sockets com `sys_addpollfn`.
  - No Windows: `WSAStartup` uma vez, `closesocket`, `ioctlsocket(FIONBIO)`.
  - Porta ocupada → `pd_error(x, "gaiaball: porta %d em uso")` e o objeto fica inerte.
  - Um cliente por vez: o novo fecha o antigo. Depois de 101, aceita só quadros binários; ping recebe pong.
  - Canais (só envia se `gensym(nome)->s_thing` existe):

    | Canal | Valor |
    |---|---|
    | `gaia.lat`, `gaia.lon`, `gaia.speed`, `gaia.bearing`, `gaia.settled` | do Nav; só com `valid = 1` |
    | `gaia.acc.x`, `gaia.acc.y`, `gaia.acc.z` | mg |
    | `gaia.quat` | lista `w x y z` |
    | `gaia.mic.rms`, `gaia.mic.peak` | listas `L R`; só com `audio_valid` |
    | `gaia.breath` | 0/1 |
    | `gaia.sensors` | lista `roll pitch yaw accX accY accZ co2`, com Euler de `gb_euler_legacy` e CO₂ = 0 até o primeiro pacote |
    | `gaia.co2` | ppm |
    | `gaia.co2.trigger` | 0/1 |

  - Outlet de status:
    - `connected 0|1` na mudança;
    - a cada 1000 ms, enquanto conectado: `rate <pacotes/s>` e `gap <maior intervalo, ms>`;
    - com 2000 ms sem pacotes ou socket fechado: `connected 0`, e `gaia.breath 0` e `gaia.co2.trigger 0` enviados uma vez (Foco 4).
  - `free`: fecha os sockets, `sys_rmpollfn`, `clock_free`.

- [ ] **Passo 1:** `make -C pd-gaiaball` (compila o external com `pd-lib-builder`, usando os cabeçalhos do Pd instalado ou `PDINCLUDEDIR`) e `make -C pd-gaiaball test`. Esperado: compila sem avisos; os testes passam.
- [ ] **Passo 2: ponta a ponta local**, se `pd` estiver instalado (se não estiver, pedir ao Nycholas `sudo apt install puredata` e esperar o OK):
  - `pd -nogui -stderr -path pd-gaiaball -open pd-gaiaball/tests/e2e/listen.pd &`;
  - `node pd-gaiaball/tests/e2e/send-goldens.mjs`;
  - esperado no stderr: `gaia.acc.x: 1000`, `gaia.breath: 1`, `gaia.co2: 1000`, `gaia.lat: …` (do golden de navegação A);
  - a segunda conexão substitui a primeira;
  - 2 s depois de fechar: `gaia.breath: 0`.
- [ ] **Passo 3: ponta a ponta com a config-app:** `npm run dev` + Playwright em `http://localhost:5173/?demo=1`. O Pd do Passo 2 recebe `gaia.*` e o chip mostra "Pd: conectado".
- [ ] **Passo 4:** commit `feat(pd): [gaiaball] external replaces the Python bridge`.

### Tarefa 13: CI do external, pacote Deken e publicação da config-app

**Arquivos:**
- Criar `.github/workflows/pd-gaiaball.yml`:
  - matriz: `ubuntu-latest` (amd64), `ubuntu-24.04-arm` (arm64), `windows-latest` (MSYS2 MinGW64) e `macos-latest` (binário único `x86_64` + `arm64`);
  - precisão do Pd: `floatsize` 32 e 64;
  - checkout com submódulos; cabeçalhos do Pd baixados do código-fonte `pure-data` numa tag fixa;
  - roda `make test` e `make`, e sobe os binários;
  - um job final junta tudo em `gaiaball/` e roda `deken package --version <tag ou 0.0.0-dev>` com a imagem `registry.git.iem.at/pd/deken`;
  - o artefato é o `.dek`, mais o `.dek.txt` e o `.dek.sha256`.
- Criar `.github/workflows/config-app-pages.yml`:
  - dispara em push na `main` com mudanças em `config-app/**`, e por `workflow_dispatch`;
  - `npm ci`, `npm test`, `PAGES_BASE=/gaiaball/ npm run build`, `actions/upload-pages-artifact`, `actions/deploy-pages`.
- Criar `config-app/vite.config.ts`: `export default defineConfig({ base: process.env.PAGES_BASE ?? '/' })`. O `npm run dev` e o `e2e-smoke` continuam em `/`.
- Atualizar `pd-gaiaball/PUBLICAR-NO-DEKEN.md` (`:63-82`): o nome do workflow e do artefato reais. O exemplo do nome do `.dek` passa a vir do primeiro artefato real.

- [ ] **Passo 1:** validar os YAML: `actionlint`, se disponível; senão, `python3 -I -c "import yaml…"` para conferir a sintaxe.
- [ ] **Passo 2:** `cd config-app && PAGES_BASE=/gaiaball/ npm run build`. Esperado: o `dist/index.html` referencia `/gaiaball/assets/…`. Depois, `npm run build && node e2e-smoke.mjs` continua passando.
- [ ] **Passo 3:** commit `ci: build and package [gaiaball] for Deken; publish the config-app to GitHub Pages`.
- [ ] **Passo 4 (só com OK do usuário):** `git push` da branch para rodar o CI. Conferir que o nome do `.dek` contém todas as arquiteturas da lista do `PUBLICAR-NO-DEKEN.md`.

---

## Parte 3 — Modo palco / modo ajuste

### Tarefa 14: Modo palco

**Arquivos:**
- Modificar `config-app/src/viz/orientation-scene.ts`: `pause()` e `resume()` públicos, que envolvem o `stop()` e o `start()` privados (`:169-182`). `resume` respeita o `document.hidden`.
- Modificar `config-app/src/panels/sensors-panel.ts`, `metrics-panel.ts` e `magcal-panel.ts`, e os laços de `main.ts`: cada `mount*` passa a devolver `{ setPaused(p: boolean): void }`. Pausado: os laços pulam o trabalho, os ouvintes não guardam nada nas filas, e a fila é limpa ao retomar.
- Criar `config-app/src/stage/stage-mode.ts`: controlador puro.
- Criar `config-app/src/stage/stage-bar.ts`, a barra do palco:
  - status do Pd (vem do `PdBridge`);
  - medidor do maior dos dois mics na medida ativa (vem de `'imu'`), redesenhado no máximo a 10 Hz;
  - indicador de pulso (`ImuSample.breath`);
  - sensibilidade em dBFS, quando a fonte é "Valor fixo": escreve o `0x2B`, lê de volta e, se não pegou, mostra "Desbloqueie com o PIN no painel Conexão";
  - "controlado pelo potenciômetro", quando a fonte é o potenciômetro;
  - botões Desconectar e "Sair do modo palco".
- Modificar `config-app/src/main.ts`:
  - chave `Modo ajuste ⟷ Modo palco` no `#topbar` (via `renderSwitch`), com o texto do modo atual logo abaixo (verbatim do spec);
  - `inert` e a classe `.stage-locked` em `#viz-wrap`, `#panels` e `#panel-sensores`.
- Modificar `config-app/src/style.css`: `.stage-locked` (opacidade reduzida, `filter: grayscale(1)`, cadeado, legenda "Pausado no modo palco") e `.stage-bar`.
- Testar em `config-app/src/stage/stage-mode.test.ts`.

**Interfaces (produz):**
```ts
export type UiMode = 'ajuste' | 'palco';
export const UI_MODE_KEY = 'gaiaball-ui-mode';
export function loadUiMode(storage: Pick<Storage, 'getItem'> | null): UiMode;   // padrão 'ajuste'
export interface StageDeps {
  scene: { pause(): void; resume(): void };
  panels: { setPaused(p: boolean): void }[];
  client: { connected: boolean; pauseDiag(): Promise<void>; resumeDiag(): Promise<void> };
  setLocked(on: boolean): void;   // inert + .stage-locked
  showBar(on: boolean): void;
  storage: Pick<Storage, 'getItem' | 'setItem'> | null;
}
export class StageController {
  constructor(deps: StageDeps);
  get mode(): UiMode;
  setMode(m: UiMode): Promise<void>;
  onConnected(): Promise<void>;   // no palco, volta a desfazer a assinatura de diag numa reconexão
}
```

- [ ] **Passo 1: testes que falham:**
  - `setMode('palco')`: chama `scene.pause`, `setPaused(true)` em todos os painéis, `pauseDiag`, `setLocked(true)` e `showBar(true)`, e grava `'palco'`;
  - `setMode('ajuste')`: faz o inverso, com `resumeDiag`;
  - sem conexão, `setMode('palco')` não chama `pauseDiag`, mas `onConnected()` chama;
  - `loadUiMode` de `null` ou `'xyz'` → `'ajuste'`;
  - repetir `setMode('palco')` não chama `pauseDiag` duas vezes.
- [ ] **Passo 2:** `npm test`. Esperado: FAIL.
- [ ] **Passo 3:** implementar.
- [ ] **Passo 4:** `npm test && npm run build && node e2e-smoke.mjs`. No e2e, acrescentar "Modo palco" e "Pausado no modo palco". Esperado: PASS.
- [ ] **Passo 5:** Playwright em `?demo=1`:
  - no palco, o Tab não entra nos painéis;
  - o 3D para;
  - os gráficos param;
  - a barra atualiza;
  - a ponte para o Pd continua (status "conectado" se o Pd da Tarefa 12 estiver aberto);
  - na volta, tudo retoma.
- [ ] **Passo 6:** commit `feat(app): stage mode pauses the UI and keeps only the Pd stream and breath controls`.

---

## Documentação final e cópia para o site

### Tarefa 15: Guia do protocolo, READMEs e cópia idêntica no site

**Arquivos (`gaiaball`):**
- `docs/protocolo-ble.md`:

  | Onde | O que muda |
  |---|---|
  | tabela de tipos `:353-363` | IMU 27 B, MTU 30, **versão 2** |
  | seção IMU `:367-407` | bytes 16–21 em mg; flags com o bit 4; áudio em u8 meio-dB nos offsets 23–26 (`dBFS = valor/2 − 120`); nota da aceleração `:401-402`; MTU 30 em `:407` |
  | Sensors e TLVs (`:854`, `:867` e a tabela de registradores) | sentido do limiar e bits 4 e 5; `0x11`/`0x12` só no adaptativo; `0x15` aposentado; `0x2B`–`0x2E` |
  | exemplo em JavaScript `:1011-1146` | `MIN_SIZE` `:1019` com `0x01: 27`; versão por tipo (`{0x01: 2, demais: 1}`) no `headerOk` `:1026-1028`; `decodeImu` `:1030-1047` em mg, áudio `u8/2 − 120`; o bit de sopro |
  | exemplo em Python `:1233-1359` | `decode_imu` `:1254-1270` |
  | exemplo em C `:1447-1610` | `gb_imu` `:1475-1484`; `gb_decode_imu` `:1504-1520`; checagem do golden `:1581` |
  | goldens `:1616-1673` | IMU v2, IMU v2 com sopro, Sensors com bits |
  | versionamento `:1696-1721` | regra 8, com o primeiro caso real |
  | "Onde está cada coisa" `:1805-1822` | linha `:1817` → external `[gaiaball]` |

  O FAQ do MTU em `:1730` passa de 34 para 30, para o IMU. Os exemplos em Python (`MIN_SIZE` `:1242`) e em C (`n < 31` em `:1506`) também passam a 27.
- `config-app/README.md`:
  - envio ao Pd (porta, permissão do Chrome);
  - modo palco e modo ajuste;
  - gráficos (janela, faixas, todos os pacotes);
  - URL publicada.

**Arquivos (`Gaiasenses-web`, branch `feat/gaiaball-mg`):**
- `docs/gaiaball-protocolo-ble.md` = cópia exata.
- `README.md:604`: nota de cópia idêntica, se precisar.

- [ ] **Passo 1:** extrair os três exemplos do guia para a pasta de rascunho da sessão e rodar contra os goldens novos:
  - `node` para o JavaScript;
  - `python3 -I` para o Python;
  - `gcc -std=c99 -Wall` para o C.

  Esperado: os três imprimem os campos do golden: aceleração 1000/−500/151, RMS 0/−20, pico 0/−6, sopro 1.
- [ ] **Passo 2:** copiar o guia e conferir `sha256sum` igual nos dois repositórios.
- [ ] **Passo 3:** no site, `npm test && npm run patches:check`; `graphify update .`.
- [ ] **Passo 4:** commits:
  - no `gaiaball`: `docs: protocol guide for IMU v2 in mg, breath bit and detector registers`;
  - no `Gaiasenses-web`, pelo caminho: `docs: sync the Gaiaball protocol guide`.

---

## Verificação final (toda a branch)

1. **`gaiaball`:**
   - `pio test -d firmware -e native` (contagem maior que a atual) e `pio run -d firmware -e esp32dev`;
   - `cd config-app && npm test && npm run build && node e2e-smoke.mjs`;
   - `make -C pd-gaiaball test` e `make -C pd-gaiaball`.
2. **`Gaiasenses-web`:** `npm test && npx tsc --noEmit && npm run lint && npm run patches:check && npm run build`.
3. **Ponta a ponta sem bola:** Pd + `[gaiaball]` + config-app em `?demo=1`, nos modos ajuste e palco (Tarefas 12 e 14).
4. **Busca de proibidos:**
   - `grep -rn "Claude"` nos diffs;
   - nenhum `Co-Authored-By`;
   - nenhum `thresholdFixed`/`0x15` vivo fora dos comentários de reservado;
   - nenhum `ACCEL_LSB_PER_G`/`8192` na config-app e no site fora dos comentários de conversão.
5. **Revisão final da branch inteira**, no modelo mais capaz (fluxo da sub-skill).
6. **Só com OK do usuário:** push das duas branches e PRs:
   - no `gaiaball`, empilhado sobre o #3;
   - no `Gaiasenses-web`, empilhado sobre o #239;
   - texto em 1ª pessoa, como Nycholas.

## Passos manuais do Nycholas (fora do código)

- Ligar o GitHub Pages do `gaiaball` (Settings → Pages → Source: GitHub Actions).
- **Escolher a licença** do repositório e do pacote antes de publicar no Deken. Hoje não há `LICENSE` na raiz, e o `deken` publica o código-fonte junto.
- Pedir a conta no puredata.info (aprovação manual) e publicar com `deken upload`, conforme o `PUBLICAR-NO-DEKEN.md`.
- Mandar ao Gabriel a mensagem em mg. O PR do patch dele vai para `feat/gaiaball-navegacao`.
- **Publicação na mesma ocasião:** gravar todas as bolas e publicar o site novo juntos, depois do patch do Gabriel. O site do ar não lê o v2.
- Bancada, com o roteiro de `docs/bench-validation.md`:
  - sopro nos dois detectores e nas duas medidas;
  - Pd no Windows: `rate` e `gap` nos modos ajuste e palco, com o Chrome atrás do Pd;
  - macOS com alguém da equipe;
  - checar o risco antigo do Espelho lento em degraus de 3°.

## Execução

Recomendo **subagent-driven**: são 16 tarefas em três linguagens, com interfaces encadeadas (golden → decodificadores → ponte → external), e um erro que chega ao firmware ou ao protocolo custa regravar as bolas. A Tarefa 0 copia este plano para o repo antes de começar.
