# Revisão de código — Gaiaball v2

**Data:** 2026-11-07 · **Revisão sobre:** branch `feature/firmware-v2` em `b7cff6e` (gaiaball) e `feature/gaiaball-binary-protocol` (Gaiasenses-web)
**Escopo:** firmware completo (`firmware/Gaiaball/src`, 3 737 linhas), `config-app/` (~2 300 linhas) e o decoder do site (`app/[locale]/map3/ble-decode.ts`). Leitura integral dos fontes; nada foi implementado — este documento só registra achados e sugestões.
**Estado de verificação no momento da revisão:** 82/82 testes nativos, 35/35 vitest, typecheck limpo, builds PlatformIO e arduino-cli OK.

**Veredito geral:** a base está saudável — separação `logic/` (puro, testável em host) × `services/` (FreeRTOS/HW) bem executada, contratos de threading documentados classe a classe, validação TLV atômica em duas passagens, e decisões de segurança corretas (PIN amostrado no enqueue, comparação em tempo constante, CO₂ fisicamente incapaz de transmitir). Os achados abaixo são de polimento, dívida residual da migração ICM-20948→GY-91 e preparação para o futuro — **nenhum é bloqueador para a bancada**.

---

## 1. O que está bem resolvido (e deve ser preservado)

| Decisão | Evidência |
|---|---|
| Lógica pura compilada e testada no host, sem HW | `platformio.ini` (env `native` com `build_src_filter = +<src/logic/> -<src/services/>`) |
| Contrato de threading escrito no header de cada serviço | `BleService.h:51-67`, `Co2Service.h:23-30`, `AudioService.h:26-32`, `ConfigStore.h:16-22` |
| Autorização de comando amostrada no momento do write (evita TOCTOU com slot reutilizado) | `BleService.h:26-29` (`unlockedAtEnqueue`), consumida em `App.cpp:315-318` |
| PIN: comparação em tempo constante + lockout 5 falhas/30 s por slot | `CommandGate.cpp` (laço de XOR; `kMaxFailures`/`kLockoutMs`) |
| TLV: valida tudo, depois comita tudo (write parcial é impossível), com regras cruzadas (pinos de LED distintos, LP>HP) | `TlvCodec.cpp:167-204` |
| PIN nunca serializado de volta | `TlvCodec.cpp:147` |
| CO₂ listen-only por construção (UART sem pino TX — o SC8 emudece se receber qualquer byte) | `Co2Service.cpp:12-15, 27` |
| Notify só com assinante (early-out por máscara atômica por slot) | `BleService.cpp:190-195`, `releaseSlot` limpa a máscara (`BleService.cpp:222-230`) |
| Queue da IMU com drop-oldest (a amostra mais nova sempre entra) | `ImuService.cpp:246-252` |
| Logger com rate-limit (20 linhas/s) e formatação fora do lock | `Logger.cpp:14-16, 44-78` |
| Wi-Fi explicitamente desligado no boot (coexistência BLE destruiria o pacing) | `App.cpp:46-55` |

---

## 2. Código e menções legadas que não fazem mais sentido no v2

A migração ICM-20948/DMP → GY-91/Madgwick (commit `57f237c`) deixou resíduos pequenos mas reais:

### 2.1 `sanitizeQuat()` — código morto da era DMP *(remoção recomendada)*
A função existia para reconstruir `w` a partir das 3 componentes vetoriais que o DMP publicava; o Madgwick entrega o quaternion completo e renormalizado. **Nenhuma chamada em produção** — só `test/test_quat/test_quat.cpp`. O próprio header ainda a descreve em termos do DMP:

> `QuatMath.h:9` — *"Rebuilds a unit quaternion from the DMP's vector part (q1..q3)"*

Sugestão: remover função + testes correspondentes (ou, se preferir manter como documentação histórica, mover o comentário para dizer explicitamente que é herança sem uso).

### 2.2 `Median5` — filtro planejado que nunca foi ligado *(remoção recomendada)*
`Filters.h` traz `Ema` (usado pelo `PotService`) e `Median5`, que **não é referenciado por nenhum serviço** — a mediana de 5 ficou no pipeline do site (`use-sensor-smoothing.ts`), por decisão de projeto (evitar dupla suavização). Só `test/test_filters` a exercita. Sugestão: remover classe + testes, ou anotar por que está reservada.

### 2.3 `quatToEulerLegacy()` em C++ — decidir o papel *(decisão, não bug)*
`QuatMath.cpp:62-79` implementa a convenção Euler legada, mas o firmware **nunca envia Euler** — a conversão vive nos decoders TS (`ble-decode.ts:69-97` no site; `euler.ts` no config-app). O uso é só em `test_quat`, onde funciona como *especificação executável* da convenção (paridade com o patch Pd). Vale a pena uma escolha consciente: (a) manter, renomeando o teste para deixar claro que é teste de contrato; ou (b) remover e deixar o contrato apenas nos decoders TS (que têm seus próprios testes). Manter sem anotação é o pior dos dois mundos.

### 2.4 Comentários que ainda dizem "DMP" *(correção de texto, 5 min)*
O hardware não tem mais DMP em uso; estes comentários descrevem o presente com vocabulário do passado:

- `GaiaConfig.h:15-16` — *"Drives BOTH the notify throttle and the **DMP sample rate** (gyro/accel divisor picked so the **DMP** runs at or just above this rate)"* → é o `SMPLRT_DIV` do MPU-9250 (`Mpu9250.cpp:99-105`).
- `Packets.h:49` — *"`int16_t accX, accY, accZ;  // raw **DMP** counts`"* → contagens cruas do MPU-9250 (±4 g, 8192 LSB/g).
- `config-app/src/panels/sensors-panel.ts:6` — *"Acceleration: g — **DMP** full scale is ±4 g"*.

(As menções em `Mpu9250.h:12`, `Fusion.h:9`, `firmware/README.md:126` e `protocol.md:119` são contexto histórico intencional — "substitui o DMP do ICM-20948" — e devem ficar.)

### 2.5 Miudezas
- `VirtualCo2Pulse::pending()` (`VirtualCo2Pulse.cpp:23-25`) não tem chamador em produção — é API usada só pelos testes de timing. Inofensivo; anotar ou remover.
- `Co2Service::kFallbackPpm = 600` (`Co2Service.h:33-34`) reproduz o `CO2_NORMAL_VALUE` legado quando `co2Enabled=0`. É intencional (compatibilidade com o site), mas é um número mágico herdado — confirmar que `protocol.md` documenta esse comportamento para quem ler só a spec.

---

## 3. Arquitetura e separação de responsabilidades

### 3.1 `App` espelha 7 escalares da config em dois lugares *(risco de drift)*
`App::begin()` copia `imuNotifyHz/co2AlertPpm/audioEnabled/potControls/audioBpf/audioHpHz/audioLpHz` (`App.cpp:60-66`) e `applyConfigIfDirty()` repete as mesmas 7 atribuições (`App.cpp:185-191`). Um campo novo esquecido em um dos dois lados vira bug silencioso (config aplicada no boot mas não a quente, ou vice-versa). Sugestão: extrair um `void applySnapshot(const GaiaConfig&)` privado chamado pelos dois pontos.

### 3.2 Um único acesso "fora do padrão" à config viva
`App::maybeNotifyCo2()` lê `store_.get().co2Enabled` direto (`App.cpp:219`), enquanto todo o resto do App usa os espelhos locais. É permitido pelo contrato do `ConfigStore` (leitura escalar na App task), mas é o único ponto que quebra o padrão — espelhar `co2Enabled_` como os demais remove a exceção.

### 3.3 O pulso virtual mora no serviço errado *(a nota de threading mais frágil do firmware)*
`Co2Service` carrega duas responsabilidades com regras de thread opostas: a leitura real (task própria "co2") e o pulso virtual, que é **App-task-only dentro de um serviço cuja task é outra** — a restrição precisa ser lembrada em comentário (`Co2Service.h:26-27`) e honrada à distância por quem chama. Como `VirtualCo2Pulse` já é `logic/` puro, ele poderia ser um membro direto do `App` (que é quem dispara, consulta e reporta), e o `Co2Service` ficaria 100 % "sensor real". Hoje funciona; é uma simplificação de manutenção, não um bug.

### 3.4 Idade do CO₂ calculada duas vezes
O serviço rastreia `lastFrameMs_` (`Co2Service.cpp:83`) e o App re-deriva a mesma informação observando `frameCount()` a 20 Hz para montar o `ageS` do pacote (`App.cpp:207-213`, com resolução degradada ao tick). Expor `lastFrameMs()` do serviço eliminaria o rastreio paralelo e daria `ageS` exato.

### 3.5 `AudioService` é singleton disfarçado
O buffer de DMA é `static` local (`AudioService.cpp:98`, 2 KB) — uma segunda instância compartilharia o buffer silenciosamente. Hoje só existe uma; vale ou torná-lo membro (`int32_t buf_[kFrames*2]` no objeto) ou documentar "uma instância só" no header.

### 3.6 Validações espelhadas em dois lugares (C++ e TS) sem teste de paridade
As regras "pinos de LED distintos" e "LP > HP" existem no firmware (`TlvCodec.cpp:170-187`) **e** copiadas no painel (`config-panel.ts:246-282`, comentários "Mirror of the firmware rule"). A cópia é necessária para UX (mensagem antes do write), mas nada hoje garante que as duas implementações concordem. Ver sugestão de teste de contrato em §6.

### 3.7 Persistência da config como blob binário da struct
`ConfigStore::save()` grava `memcpy` da `GaiaConfig` (`ConfigStore.cpp:65`) e o load exige versão E tamanho exatos (`ConfigStore.cpp:45-46`). Correto e simples, mas cada bump de schema (já estamos no v5) **descarta a config do usuário em campo**. Alternativa futura: persistir a serialização TLV (ids estáveis, registros desconhecidos ignorados) — migrações viram no-op e o load fica tolerante a versões. Não fazer agora; fazer antes de existirem bolas em produção com config valiosa.

---

## 4. Performance

### 4.1 IMU por polling de 2 ms — funciona, mas o chip já oferece coisa melhor
A task da IMU acorda a cada 2 ms e lê `INT_STATUS` por I²C (`ImuService.cpp:20, 128`; `Mpu9250.cpp:107-111`): ~500 transações/s só para perguntar "tem dado?", mais jitter de amostragem de até 2 ms. O registrador `INT_ENABLE` **já é configurado** para RAW_DATA_RDY (`Mpu9250.cpp:73`) — se o pino INT do GY-91 estiver roteado na placa (conferir no netlist/KiCAD), trocar o polling por interrupção GPIO + task notification elimina o tráfego I²C ocioso e o jitter. Se o INT não estiver roteado, reduzir o custo adaptando o período de poll à taxa configurada (a 50 Hz, acordar a cada 2 ms é 10× mais frequente que o necessário).

### 4.2 Taxa de 200 Hz × intervalo de conexão BLE — validar na bancada
O firmware não negocia parâmetros de conexão; o intervalo fica a critério do central (Web Bluetooth não permite ajustá-lo pelo site). Com intervalo típico de 30–50 ms, 200 notifies/s exigem 6–10 notifies por evento de conexão — pode funcionar (mbufs do NimBLE), mas pode saturar e derrubar a taxa efetiva. Sugestão: no roteiro de bancada, medir "Taxa" no config-app com `imuNotifyHz=200`; se ficar abaixo do alvo, chamar `updateConnParams()` após a conexão pedindo 7,5–15 ms.

### 4.3 Drivers legados de I2S e ADC *(dívida de manutenção, não de velocidade)*
`driver/i2s.h` (`AudioService.cpp:4`) e `driver/adc.h` (`PotService.cpp:4`) são as APIs legadas do IDF 4.x — removidas/depreciadas no IDF 5 (core Arduino 3.x). O pin do PlatformIO em `espressif32@6.12.0` congela isso com segurança hoje; registrar que a eventual subida de core exige migrar para `esp_driver_i2s`/`esp_adc` (oneshot).

### 4.4 O que já está certo em performance (não mexer)
Zero alocação de heap nos caminhos quentes (pacotes em stack, `setValue` pré-dimensionado — `BleService.cpp:160-166`); formatação de log atrás de atomic + default OFF; FFT no navegador em vez do ESP32; chunk de áudio descartado sem assinante antes de qualquer trabalho de BLE (`App.cpp:276-279`); `double` usado só em acumuladores de RMS onde precisão importa (`AudioService.cpp:130`).

---

## 5. Robustez e segurança

### 5.1 Watchdog cobre só a App task
`Diagnostics::watchTask()` é chamado apenas em `App::taskLoop()` (`App.cpp:110`); as tasks imu/audio/co2 não têm WDT. Um travamento na audio task (ex.: `i2s_read` com `portMAX_DELAY`, `AudioService.cpp:102`, se o DMA parar) degrada silenciosamente — o diag até continuaria reportando `audioOk=true` do último estado. Sugestão: inscrever as três tasks no TWDT (com `i2s_read` passando a timeout finito) ou, mais barato, a App task vigiar `frameCount`/contadores por subsistema e reiniciar via `esp_restart()` após N segundos de silêncio anômalo.

### 5.2 Característica de config legível sem desbloqueio *(decisão consciente — só documentar)*
A leitura TLV não exige PIN (`BleService.cpp:291-295`; só o write é gated em `onConfigWrite`). O PIN nunca sai, mas thresholds, nome e pinos ficam legíveis para qualquer central conectada. Para uma instalação artística o risco é irrelevante; vale uma linha no `protocol.md` declarando que é por design.

### 5.3 Pontos já bem tratados
Race do `m_subscribedVec` do NimBLE: conhecida, documentada e mitigada (`BleService.h:63-65`). Corrupção de NVS: strings re-terminadas no load (`ConfigStore.cpp:49-51`). Boot sem sensores: cada `begin()` falha graciosamente e o resto do sistema sobe (`ImuService.cpp:103-106` idle forever; `App.cpp:72-87` só loga erro). GPIO2 (strapping) sempre dirigido LOW no init (`LedController.cpp:8-14`).

---

## 6. Testes — cobertura e lacunas

**Hoje:** 82 asserções nativas em 10 suítes cobrindo `logic/` (parser SC8 com golden frame do datasheet, fusão com convenção body→earth, TLV incluindo ids reservados, gate com lockout, DSP, throttle); 35 vitest em protocol/metrics/presets do config-app; 117 testes do decoder no repo do site. `services/` e `App` não têm testes — consequência aceita da arquitetura (dependem de FreeRTOS/NimBLE/HW real).

Lacunas que valem fechar, em ordem de retorno:

1. **Teste de contrato C++ ↔ TS com fixtures compartilhadas.** Hoje a paridade entre `Packets.h`/`TlvCodec.cpp` e `packets.ts`/`tlv.ts` é mantida à mão. Um diretório de vetores golden (bytes hex + valores esperados, JSON) consumido pelos dois lados (unity e vitest) pegaria qualquer divergência de offset/escala no CI — inclusive as regras espelhadas de §3.6.
2. **Mais lógica do `App` extraída para `logic/`.** A política de notify do CO₂ ("mudou OU keepalive 1 Hz" + flags + `ageS`, `App.cpp:206-246`) e o cálculo de Hz do diag (`App.cpp:248-257`) são funções puras presas num arquivo não testável. Extraí-las seguiria exatamente o padrão que já deu certo com `NotifyThrottle`.
3. **Painéis do config-app sem testes.** `config-panel.ts` concentra validação + dirty-tracking + read-back (374 linhas) e é todo testável com `happy-dom` — o caminho "Aplicar → read-back → rejeitados" é o mais valioso.

---

## 7. Sugestões de expansão futuras (pertinentes ao projeto)

Em ordem aproximada de valor/custo:

1. **CI (GitHub Actions).** Hoje toda a verificação (2 builds + 82 + 35 testes + tsc) roda na máquina local. Um workflow com `pio test -e native`, `pio run`, `arduino-cli compile`, `vitest`, `tsc --noEmit` é ~1 h de trabalho e protege o PR #1 e tudo que vier depois.
2. **Calibração do magnetômetro (hard/soft-iron) com persistência em NVS.** O Quat9 usa o AK8963 cru (`ImuService.cpp:168-177`, só o ajuste de fábrica ASA em `Mpu9250.cpp:92`). Dentro de um globo com eletrônica e LEDs, o offset de ferro duro praticamente garante yaw distorcido — hoje a mitigação é "use Quat6". Uma rotina "gire a bola em forma de 8" guiada pelo config-app (que já tem vista 3D e canal de comandos/TLV livre) + ids TLV novos para o offset tornaria o modo absoluto utilizável.
3. **Atualização OTA.** A bola é uma esfera fechada; regravar firmware pelo cabo exige abri-la. Partição OTA + upload via característica BLE dedicada (ou Wi-Fi temporário ativado por comando gated) é a expansão de maior impacto operacional para um objeto lacrado em exposição. (Era anti-goal da etapa v2 — a arquitetura não impede.)
4. **Identidade de dispositivo no protocolo.** Os pacotes têm `version/seq` mas nenhum id de hardware. Para instalações com mais de uma bola (ou bancada com duas), um `deviceId` (MAC curto) no `DiagPacket` + exibição no config-app evita confusão de qual bola está falando. Cabe nos bytes de folga sem quebrar o v1 (bump de versão de protocolo já previsto no campo `version`).
5. **Persistir config como TLV** (§3.7) antes de haver bolas em campo com configuração valiosa.
6. **Gravação e replay no config-app.** As séries já estão no Plotly; exportar CSV/JSON (e re-alimentar o modo demo com uma gravação) permitiria afinar o patch Pd e os filtros com dados reais capturados na bancada, sem a bola presente.
7. **Latência fim-a-fim real.** `timestampUs` do dispositivo já viaja em todo pacote IMU; o config-app mostra só "idade da amostra" local. Estimar o offset de clock (handshake simples via característica de comando) e plotar latência BLE real fecharia a última meta de aceitação do plano (<50 ms) com número de verdade.
8. **Monitor de alimentação.** Se a bola for operar por bateria, uma leitura ADC de tensão + flag no diag (bytes livres) evita "morreu no meio da exposição" — hoje não há telemetria de energia.

---

## 8. Resumo executivo dos achados

| # | Achado | Tipo | Severidade | Onde |
|---|---|---|---|---|
| 2.1 | `sanitizeQuat` morto (era DMP) | legado | baixa | `QuatMath.cpp/.h:9` |
| 2.2 | `Median5` nunca ligado | legado | baixa | `Filters.h` |
| 2.3 | `quatToEulerLegacy` C++ sem papel declarado | legado/decisão | baixa | `QuatMath.cpp:62-79` |
| 2.4 | Comentários "DMP" desatualizados (3 arquivos) | legado | baixa | `GaiaConfig.h:15`, `Packets.h:49`, `sensors-panel.ts:6` |
| 3.1 | Espelhos de config duplicados no App | arquitetura | média | `App.cpp:60-66` × `185-191` |
| 3.3 | Pulso virtual App-task-only dentro do Co2Service | arquitetura | média | `Co2Service.h:26-27` |
| 3.4 | Idade do CO₂ derivada 2× | arquitetura | baixa | `App.cpp:207-213` |
| 3.6 | Validações espelhadas C++/TS sem teste de paridade | testes | média | `TlvCodec.cpp:170-187` × `config-panel.ts:246-282` |
| 3.7 | Config persistida como blob (bump apaga config) | arquitetura | média (futuro) | `ConfigStore.cpp:45-66` |
| 4.1 | Polling I²C de 2 ms em vez do INT | performance | baixa | `ImuService.cpp:20,128` |
| 4.2 | 200 Hz × intervalo de conexão não negociado | performance | média (validar) | `BleService.cpp` |
| 4.3 | Drivers I2S/ADC legados (IDF 4) | manutenção | baixa (pinado) | `AudioService.cpp:4`, `PotService.cpp:4` |
| 5.1 | WDT só na App task | robustez | média | `App.cpp:110`, `AudioService.cpp:102` |
| 5.2 | Config legível sem PIN (documentar que é por design) | segurança | baixa | `BleService.cpp:291-295` |

Nenhum achado invalida o plano atual: **merge dos branches → flash → roteiro de bancada** (`docs/bench-validation.md`) continua sendo o próximo passo; desta lista, só 4.2 merece entrar no roteiro de bancada (medir a taxa com 200 Hz configurados).

---

*Revisão produzida por Claude (Fable 5) a pedido, sem alterações de código. Evidências referem-se ao estado dos arquivos em `b7cff6e`.*
