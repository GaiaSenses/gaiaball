# Gaiaball v2 — firmware modular, relatório HTML e app de configuração

## Contexto

A Gaiaball é uma interface esférica (~40 cm) com um ESP32 DevKit + IMU ICM-20948: o usuário gira o globo físico e a rotação deve espelhar-se em um globo virtual no frontend GaiaSenses (Mapbox GL em projeção globo) via BLE. O firmware atual (`/home/nyck/cti/gaiaball/firmware-original.ino`, **não deve ser modificado**) apresenta: movimento "alucinado", travamentos, latência crescente e conexão BLE instável. Todos os sintomas têm causas identificáveis no código.

O usuário pediu: (1) análise profunda e independente (além do relatório do ChatGPT em `gaiaball/relatorio.md`), (2) firmware novo em estrutura separada, modular, multi-arquivo, documentado em inglês técnico, (3) relatório HTML interativo em português (problema → correção → impacto), (4) app web standalone de configuração com presets e visualização 3D em tempo real para comparar movimento real × simulado.

## Decisões confirmadas pelo usuário

- **Toolchain:** dual — compila no Arduino IDE **e** no PlatformIO.
- **Protocolo:** binário compacto; o decoder do Gaiasenses-web será atualizado junto.
- **App de config:** standalone em `gaiaball/config-app/` (Web Bluetooth, Chrome/Edge, local).
- **Escopo:** manter todos os subsistemas (IMU, 2 mics, CO₂, pot, LEDs), cada um em módulo isolado e desativável por configuração.
- **Orientação:** configurável Quat6/Quat9; padrão **relativa** (Quat6, sem magnetômetro) com comando "zerar orientação".
- **Conexões BLE:** 2 centrais simultâneas (site + config-app ao mesmo tempo, para ajustar filtros vendo o globo real).
- **Proteção:** PIN de desbloqueio simples para comandos sensíveis (config/reboot/reset/calibração); leitura de dados e LED legado continuam abertos.
- **Frota:** dispositivo único, único cliente (o site) → sem modo JSON em runtime no firmware; o fallback JSON fica só no decoder do site.
- **Sensor de CO₂:** modelo informado "SC8" (sem datasheet público confirmado); ver seção CO₂ abaixo.

### Escolhas do construtor (minhas, declaradas)
- Presets ficam no app (localStorage + export/import JSON); o dispositivo guarda só a config ativa em NVS.
- Metas de aceitação: notify de orientação 50–60 Hz, latência medida < 50 ms, 24 h sem travar, zero NaN.
- Stack BLE: **NimBLE-Arduino** (menos RAM, melhor controle; instalável pelo Library Manager do Arduino IDE e pinada no platformio.ini). Compatível com Web Bluetooth.
- UI do config-app em pt-BR; código e comentários em inglês técnico.
- Nome BLE configurável via NVS (padrão mantém `Globo_1`); o site filtra por UUID de serviço, não por nome.
- **Contrato com o patch Pure Data preservado**: o decoder do site reproduz a convenção legada na saída (mesmos ângulos de Euler para o mesmo movimento físico — remap `qx=q2, qy=q1, qz=-q3` aplicado no decoder — e aceleração em contagens brutas do DMP, passthrough). Verificar durante a implementação lendo o patch Pd e `Gaiasenses-web/docs/musico/vocabulario.md` (L48-70). A convenção vira parte da spec em `protocol.md`.
- **Biblioteca SparkFun ICM-20948 vendorada** em `src/` com `ICM_20948_USE_DMP` já habilitado (a lib exige editar `ICM_20948_C.h` à mão — vendorar elimina esse passo manual nos dois toolchains).
- Suavização por slerp no firmware existe mas fica **desligada por padrão** — o site já suaviza (mediana+EMA); dupla suavização adicionaria latência.
- PIN: opcode `0x20` + PIN (guardado na NVS, padrão documentado, alterável via config após desbloqueio). Estado de desbloqueio é por conexão e expira na desconexão.

## Diagnóstico independente (resumo; detalhe completo vai no relatório HTML)

Verificados por leitura de `firmware-original.ino` (linhas citadas):

1. **`q0 = sqrt(1 - (q1²+q2²+q3²))` sem clamp** (L548) → NaN quando o argumento fica negativo; sem normalização do quaternion → **"globo alucina"**.
2. **FIFO do DMP sem drenagem** — 1 pacote lido por iteração (L534) com loop travado por `delay(12)` (L619) + `delay(10)` (L637): DMP produz ~56 Hz (divisor 1, L434; o comentário "1Hz" está errado), loop consome <40 Hz → backlog crescente = **latência crescente + saltos ao estourar a FIFO**.
3. **`co2Doc` só é preenchido dentro do bloco `haveQuat`** (L582-584), mas é notificado sempre (L613-617): sem quat, o site recebe `{}` e `parsed.co2.ppm` lança TypeError **sem try/catch no listener do site** → amostras perdidas silenciosamente.
4. **GPIO2 triplo**: `ledPin` (L48), `LED_SOM_PIN` (L99) e `CS_PIN` (L18) no mesmo pino de strapping; o callback BLE sobrescreve o LED de som (L74).
5. **JSON + `String` alocados a cada iteração** (L537-539, L606-617) → fragmentação de heap; notify mesmo sem amostra nova; sem controle de MTU.
6. **Áudio**: média de |x| em vez de RMS, sem remoção de DC (L205-251); `i2s_read` bloqueante até 10 ms no caminho crítico; `MIC_IIR_ALPHA 0.99` a 50 Hz → resposta ~2 s; baseline fixa para sempre após 2 s de calibração bloqueante no boot (L253-271).
7. **Parser CO₂** assume frame alinhado (L333-357), sem máquina de estados de ressincronização nem validade/timeout do valor. **Provável bug de checksum**: o código soma bytes 0..14 e compara com o byte 15, mas a referência pública do frame 0x42 0x4D de 16 bytes (write-up de engenharia reversa, spezifisch.codeberg.page 2022-08-23) define checksum = soma dos bytes 0..13 — se o byte 14 ≠ 0, o checksum legado falha sempre e `co2_real` nunca atualizou. Confirmar na bancada com o logger de frames brutos.
8. **Init da IMU em loop infinito** (L414-428) → periférico ausente trava o sistema todo.
9. **Remap de eixos ad-hoc** (`qx=q2, qy=q1, qz=-q3`, L558-561) sem calibração de montagem; Euler com gimbal lock em pitch ±90°.
10. **Quat9 usa magnetômetro** → interferência magnética do ambiente vira outra fonte de "alucinação".
11. **Print serial de aceleração a cada pacote** (L593-598) bloqueia o loop; sem níveis de log, sem watchdog, sem métricas, sem seq/timestamp.

## Reconhecimento do frontend (verificado)

- Todo o BLE do site está em `Gaiasenses-web/app/[locale]/map3/ble-control.tsx` (UUIDs L40-43; parse JSON L95-119 — **sem try/catch**).
- Pipeline: notify → `espResponse {quat, euler, acc}` → calibração por baseline + mediana(5) + EMA(α=0.24) em `use-sensor-smoothing.ts` → modo padrão `"pd"` envia **Euler + acc + CO₂** ao patch Pure Data, que devolve lat/lng; `easeTo` no Mapbox a 30 Hz.
- CO₂ > limiar (1200, ajustável) dispara composição (`use-ble-sensor.ts` L90-138).
- **Mudança mínima no site**: um decoder binário em `ble-control.tsx` que produza os mesmos objetos `espResponse`/`espCo2Response` (Euler computado no decoder a partir do quat) — o resto do pipeline fica intacto. Fallback: se o 1º byte for `{` (0x7B), parse JSON (compatível com firmware antigo).

## Entregáveis e estrutura

```
gaiaball/
├── firmware-original.ino          # intocado
├── relatorio.md                   # intocado (ChatGPT)
├── firmware/                      # ① firmware novo
│   ├── platformio.ini             # env esp32dev; src_dir=Gaiaball; NimBLE/ArduinoJson pinadas
│   ├── README.md                  # build nos 2 toolchains, pinagem, flash
│   ├── docs/
│   │   ├── protocol.md            # spec binária completa (fonte da verdade)
│   │   └── architecture.md        # tarefas, filas, prioridades
│   └── Gaiaball/                  # pasta de sketch (Arduino IDE abre direto)
│       ├── Gaiaball.ino           # fino: instancia App, setup()/loop()
│       └── src/                   # compilado recursivamente pelos 2 toolchains
│           ├── config/            # ConfigStore: structs de parâmetros, defaults, NVS (Preferences), versionamento
│           ├── protocol/          # Packets.h: structs packed, IDs, escalas, versão; CommandCodes.h
│           ├── imu/               # ImuService: init c/ timeout+retry, drenagem total da FIFO,
│           │                      #   sanitize (clamp+normalize+reject NaN), mounting calib (quat de montagem),
│           │                      #   modos Quat6/Quat9, zero de orientação (q_ref⁻¹·q), suavização opcional (slerp)
│           ├── audio/             # AudioService: I2S em task própria, DC removal, RMS por canal,
│           │                      #   filtro rápido+lento, histerese, baseline adaptativa
│           ├── co2/               # Co2Service: parser incremental (máquina de estados), layout de frame
│           │                      #   CONFIGURÁVEL (posição do ppm, variante de checksum 0..13/0..14),
│           │                      #   modo de diagnóstico que loga frames brutos (p/ cravar o protocolo do
│           │                      #   "SC8" na bancada), timestamp de validade, flag stale; CO₂ virtual (pulso)
│           ├── ble/               # BleService (NimBLE, até 2 conexões c/ estado por conexão): GATT, MTU 247,
│           │                      #   notify só com amostra nova, config TLV, comandos c/ PIN, diagnósticos
│           ├── leds/              # LedController central com prioridades (comando BLE < evento som < erro)
│           ├── control/           # PotService (ADC filtrado + histerese)
│           ├── diag/              # Diagnostics: Hz real, heap, FIFO overflows, erros I²S/UART, watchdog
│           └── util/              # Logger c/ níveis, RingBuffer, filtros (EMA, mediana), TimeUtils
├── config-app/                    # ② app standalone (Vite + TS + three.js, UI pt-BR)
│   ├── src/ble/                   # cliente Web Bluetooth + decoder binário + encoder de config
│   ├── src/panels/                # conexão, orientação, filtros, taxas, áudio, LEDs, presets
│   ├── src/viz/                   # cena 3D (avião/retângulo), raw × filtrado, trilha
│   └── src/metrics/               # Hz, jitter, perda por seq, idade da amostra, gráficos
└── relatorio/
    └── index.html                 # ③ relatório interativo pt-BR, arquivo único, dark/light
```

**④ Gaiasenses-web** (mudança mínima): decoder binário com try/catch em `app/[locale]/map3/ble-control.tsx` (L95-119) + atualização do README (seção BLE, ~L552). Nenhum outro arquivo muda (objetos de saída idênticos). Rodar `graphify update .` após a mudança (regra do CLAUDE.md).

## Protocolo binário (resumo; spec completa em firmware/docs/protocol.md)

Little-endian, structs packed, versão no byte 0. UUIDs de serviço e características **0001/0002/0003 preservados**; novas: `…0004` (config TLV read/write) e `…0005` (diagnósticos notify 1 Hz).

- **IMU (char 0001, 23 bytes):** `ver u8 | type u8 | seq u16 | t_us u32 | quat int16×4 (×1/16384) | accel int16×3 | flags u8` (quat válido, modo 6/9, em movimento). 50–60 Hz, só com amostra nova.
- **CO₂ (char 0003, 12 bytes):** `ver | type | seq u16 | t_ms u32 | ppm u16 | flags u8 (válido/stale/virtual) | age_s u8`. Em mudança ou keepalive 1 Hz.
- **Comandos (char 0002, write):** byte 0 = opcode. `0x00/0x01` mantêm semântica legada do LED (abertos); `0x20 + PIN` desbloqueia a conexão; **gated por PIN**: `0x10` zerar orientação, `0x11` salvar config na NVS, `0x12` restaurar defaults, `0x13` reboot, `0x14` calibração de montagem.
- **Config (char 0004, gated por PIN para escrita):** registros TLV `id u8 | len u8 | valor` (modo de orientação, taxas, α dos filtros, limiares de áudio, pinos de LED, nome BLE, PIN…). Write aplica a quente; `0x11` persiste.
- **Multi-conexão:** notificações e MTU rastreados por conexão; teste com site + config-app conectados simultaneamente.

## Fases de implementação

**Fase 1 — Firmware** (maior parte do trabalho)
1. Esqueleto dual-toolchain + `ConfigStore` + `Logger` + `Packets.h`.
2. Serviços por módulo (IMU → BLE → CO₂ → áudio → LEDs/pot → diag), cada um com task FreeRTOS própria (BLE/radio no core 0; aquisição no core 1), filas/ring buffers entre aquisição e transmissão.
3. Verificação de compilação nos **dois** toolchains (instalar `arduino-cli` e `platformio` CLI no WSL se faltarem; compilar, não gravar).

**Fase 2 — Config app**: Vite + TS; cliente BLE + decoder; painéis ligados à característica de config; presets; cena 3D; métricas. Build estático (`npm run build`) + `npm run dev` para uso local.

**Fase 3 — Gaiasenses-web**: decoder binário (com fallback JSON) em `ble-control.tsx`; try/catch; README. Typecheck/lint/testes do repo + `graphify update .`.

**Fase 4 — Relatório HTML**: todos os achados (meus + os válidos do relatorio.md, com crédito), antes/depois de código, arquitetura nova, protocolo, impacto esperado por sintoma, checklist de validação em bancada. Também publicado como artifact privado (link para compartilhar).

**Fase 5 — Documentação final**: READMEs, instruções de flash e de teste em bancada (roteiro: girar em superfície plana, medir Hz/latência/perda no config-app).

## Verificação

- Firmware: `pio run` **e** `arduino-cli compile --fqbn esp32:esp32:esp32` sem erros/warnings relevantes (gravação em hardware fica com o usuário; fornecer roteiro).
- Config app: `npm run build` limpo; abrir via Playwright para smoke test de UI (sem BLE real).
- Gaiasenses-web: `npm run lint` / typecheck / testes existentes; decoder coberto por teste unitário com pacotes binários sintéticos (incl. truncados/corrompidos → não lança).
- Relatório: abrir no navegador (Playwright) e conferir interatividade.
- Hardware (usuário, com roteiro): flash, bancada no config-app (Hz ≥ 50, perda ~0, sem NaN), teste com site + config-app conectados ao mesmo tempo, logger de frames brutos para confirmar o protocolo do sensor de CO₂, depois site.

## Fora de escopo (anti-goals)

- Não modificar `firmware-original.ino` nem `relatorio.md`.
- Não mudar o pipeline de suavização/Pd do Gaiasenses-web (só o decoder).
- Sem OTA, Wi-Fi/WebSocket no firmware ou multi-dispositivo nesta etapa (arquitetura não deve impedir depois).
