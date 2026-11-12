# Suporte a dois IMUs (ICM-20948 + GY-91/MPU-9250) — Plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Um único binário dirige tanto o ICM-20948 da bola atual quanto o GY-91/MPU-9250 das próximas unidades, com o chip escolhido pelo config-app e persistido na NVS.

**Architecture:** `Mpu9250` vira uma implementação de uma interface `ImuDriver`; `Icm20948` é a segunda. `ImuService` passa a construir o driver a partir de `cfg.imuChip` e deixa de conhecer o modelo do sensor. A aritmética de taxa/DLPF dos dois chips e a tabela de remapeamento de eixos do magnetômetro mudam para `logic/`, onde compilam e são testadas no host. O formato dos pacotes não muda.

**Tech Stack:** C++17 (Arduino-ESP32, FreeRTOS, NimBLE), PlatformIO `espressif32@6.12.0` + arduino-cli `esp32:esp32@2.0.17`, Unity para testes nativos; TypeScript + Vite + vitest no `config-app/`.

**Spec:** `docs/superpowers/specs/2026-11-12-imu-dual-chip-design.md`

## Global Constraints

- Toolchain dupla obrigatória: `pio run -d firmware -e esp32dev` **e** `arduino-cli compile --fqbn esp32:esp32:esp32 firmware/Gaiaball` compilam ao fim de toda tarefa que toque o firmware.
- Testes nativos: `pio test -d firmware -e native`. O ambiente `native` compila **só** `src/logic/` (`build_src_filter` em `platformio.ini`) — nada em `src/services/` é testável no host.
- Testes do app: `npm test` em `config-app/` (vitest).
- Baseline que não pode regredir: **131 testes nativos + 52 vitest**, todos verdes.
- Nenhum pacote do protocolo muda de tamanho ou layout. `ImuPacket` 31 B, `DiagPacket` 34 B, `SensorsPacket` 23 B, `AudioChunkPacket` 208 B, `MagChunkPacket` 106 B, `Co2Packet` 12 B.
- `ahrsFlags` do `DiagPacket` está com os 8 bits ocupados: não usar.
- Ids TLV reservados, nunca reutilizar: `0x21`, `0x27`, `0x33`. Comandos reservados: `0x00`, `0x01`.
- `firmware-original.ino` e `relatorio.md` **nunca** são modificados.
- Todo `Field` novo no config-app exige `info` (tooltip didático) — é obrigatório pela interface.
- Registradores do ICM-20948 conferidos contra o datasheet **DS-000189 rev. 1.3**; os valores deste plano orientam, não substituem.

## Review Focus

1. **`imuChip` fora de 0..2 escrito por BLE** — `tlvApply` é tudo-ou-nada: a escrita inteira deve falhar e a configuração viva ficar intacta. (Tarefa 3)
2. **Chip selecionado, sensor ausente** — 10 tentativas, IMU desligado, resto do firmware vivo, e `imuDetected` reportando "selecionado X, não encontrado". (Tarefa 7)
3. **Preset salvo antes desta mudança, carregado depois** — não pode tocar `imuChip` nem `magAxisRemap`. (Tarefa 8)
4. **`imuNotifyHz` nos extremos (10 e 200 Hz) no ICM-20948** — divisores de 16 bits dentro da faixa, nunca zero, e DLPF sempre abaixo do Nyquist da saída. (Tarefa 1)
5. **`magAxisRemap` trocado com calibração carregada** — a captura do `MagChunkPacket` passa a publicar com o remap NOVO, para que um reajuste no navegador seja coerente. (Tarefa 5)

---

## File Structure

| Arquivo | Responsabilidade |
|---|---|
| `firmware/Gaiaball/src/logic/ImuFrontEnd.h/.cpp` | **novo** — taxa, divisores e escolha de DLPF dos dois chips, como funções puras |
| `firmware/Gaiaball/src/logic/MagAxisRemap.h` | **novo** — as 24 permutações de eixos com sinal, e sua aplicação |
| `firmware/Gaiaball/src/services/ImuDriver.h` | **novo** — interface abstrata do sensor |
| `firmware/Gaiaball/src/services/Icm20948.h/.cpp` | **novo** — driver do ICM-20948 |
| `firmware/Gaiaball/src/services/Mpu9250.h/.cpp` | passa a implementar `ImuDriver`; aritmética sai para `logic/` |
| `firmware/Gaiaball/src/services/ImuService.h/.cpp` | escolhe o driver pela config; remap vem da config |
| `firmware/Gaiaball/src/logic/GaiaConfig.h` | `imuChip`, `magAxisRemap` |
| `firmware/Gaiaball/src/logic/TlvCodec.cpp` | registros `0x34`, `0x36` |
| `firmware/Gaiaball/src/services/BleService.cpp` | anexa `0x35` na leitura da config |
| `config-app/src/protocol/tlv.ts` · `panels/config-panel.ts` · `panels/metrics-panel.ts` · `main.ts` | campos, presets, rótulos, leitura ao conectar, linha de estado |

---

### Task 1: Aritmética de taxa e DLPF dos dois chips em `logic/`

**Files:**
- Create: `firmware/Gaiaball/src/logic/ImuFrontEnd.h`, `firmware/Gaiaball/src/logic/ImuFrontEnd.cpp`
- Modify: `firmware/Gaiaball/src/services/Mpu9250.cpp` (remove o namespace anônimo com `autoDlpfMode`, passa a chamar `logic/`)
- Test: `firmware/test/test_imu_frontend/test_imu_frontend.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace gaia {
  enum class ImuChip : uint8_t { None = 0, Icm20948 = 1, Mpu9250 = 2 };
  struct FrontEndPlan {
    uint8_t dlpfMode;     // índice de registro, 0..7
    float   cutoffHz;     // corte efetivo escolhido
    uint16_t gyroDiv;     // valor de divisor do giroscópio
    uint16_t accelDiv;    // ICM: divisor próprio do acelerômetro; MPU: igual a gyroDiv
    float   actualHz;     // taxa de saída que os divisores realmente produzem
  };
  FrontEndPlan planFrontEnd(ImuChip chip, uint8_t requestedHz, uint8_t forcedDlpfMode);
  float dlpfCutoffHz(ImuChip chip, uint8_t mode);
  }
  ```
  `forcedDlpfMode == 0` ou `> 7` significa automático. Base de decimação: 1000 Hz no MPU-9250, 1125 Hz no ICM-20948. Regra do automático, idêntica à atual: o DLPF mais largo cujo corte fique em `<= 0.47 * requestedHz`.

- [ ] **Step 1: Escreva o teste que falha**

```cpp
void test_mpu_auto_dlpf_matches_legacy(void) {           // preserva o comportamento atual
  const FrontEndPlan p = planFrontEnd(ImuChip::Mpu9250, 50, 0);
  TEST_ASSERT_EQUAL_UINT8(4, p.dlpfMode);                 // 20 Hz
  TEST_ASSERT_EQUAL_FLOAT(20.0f, p.cutoffHz);
  TEST_ASSERT_EQUAL_UINT16(19, p.gyroDiv);                // 1000/50 - 1
  TEST_ASSERT_EQUAL_UINT16(p.gyroDiv, p.accelDiv);
}
void test_icm_divisors_at_rate_extremes(void) {           // Review Focus 4
  for (uint8_t hz : {10, 200}) {
    const FrontEndPlan p = planFrontEnd(ImuChip::Icm20948, hz, 0);
    TEST_ASSERT_TRUE(p.gyroDiv <= 0xFF);                   // GYRO_SMPLRT_DIV é u8
    TEST_ASSERT_TRUE(p.accelDiv <= 0x0FFF);                // ACCEL_SMPLRT_DIV são 12 bits úteis
    TEST_ASSERT_TRUE(p.actualHz > 0.0f);
    TEST_ASSERT_TRUE(p.cutoffHz <= 0.5f * p.actualHz);     // nunca acima do Nyquist
  }
}
void test_forced_dlpf_is_respected(void) {
  TEST_ASSERT_EQUAL_UINT8(2, planFrontEnd(ImuChip::Icm20948, 50, 2).dlpfMode);
}
void test_cutoff_tables_differ_between_chips(void) {
  TEST_ASSERT_NOT_EQUAL(dlpfCutoffHz(ImuChip::Mpu9250, 3), dlpfCutoffHz(ImuChip::Icm20948, 3));
}
```

- [ ] **Step 2: Rode e confirme a falha**

Run: `pio test -d firmware -e native -f test_imu_frontend`
Expected: FAIL — `ImuFrontEnd.h` não existe.

- [ ] **Step 3: Implemente `ImuFrontEnd.h/.cpp`**

Duas tabelas `constexpr` de cortes de DLPF (MPU-9250: 184/92/41/20/10/5/3600 Hz nos índices 1..7, como hoje em `Mpu9250.cpp`; ICM-20948: do datasheet, `GYRO_DLPFCFG` 0..7) e os divisores de cada base. Arredondar o divisor como hoje (`(base + hz/2) / hz`, menos 1, com piso em 0).

- [ ] **Step 4: Rode e confirme que passa**

Run: `pio test -d firmware -e native -f test_imu_frontend` → PASS

- [ ] **Step 5: Faça `Mpu9250::setSampleRateHz` e `dlpfCutoffHz` chamarem `logic/`**

Sem mudar comportamento: o `GB_LOGI` existente continua, e o `autoDlpfMode` do namespace anônimo é removido.

- [ ] **Step 6: Suíte inteira + as duas toolchains**

Run: `pio test -d firmware -e native` → 131+ PASS
Run: `pio run -d firmware -e esp32dev` e `arduino-cli compile --fqbn esp32:esp32:esp32 firmware/Gaiaball` → ambos OK

- [ ] **Step 7: Commit**

```bash
git add firmware/Gaiaball/src/logic/ImuFrontEnd.* firmware/Gaiaball/src/services/Mpu9250.cpp firmware/test/test_imu_frontend/
git commit -m "refactor(imu): move rate/DLPF arithmetic for both chips into logic/, host-tested"
```

---

### Task 2: Tabela de remapeamento de eixos do magnetômetro

**Files:**
- Create: `firmware/Gaiaball/src/logic/MagAxisRemap.h`
- Test: `firmware/test/test_magremap/test_magremap.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace gaia {
  constexpr uint8_t kMagRemapCount = 24;
  constexpr uint8_t kMagRemapDefault = /* índice de {+Y, +X, -Z} */;
  void applyMagRemap(uint8_t index, const float in[3], float out[3]);
  const char *magRemapLabel(uint8_t index);  // "+Y +X -Z", para log e documentação
  }
  ```
  Índice fora de `0..23` se comporta como `kMagRemapDefault`.

- [ ] **Step 1: Escreva o teste que falha**

```cpp
void test_default_reproduces_current_behaviour(void) {
  const float in[3] = {1.0f, 2.0f, 3.0f}, want[3] = {2.0f, 1.0f, -3.0f};
  float out[3]; applyMagRemap(kMagRemapDefault, in, out);
  TEST_ASSERT_EQUAL_FLOAT_ARRAY(want, out, 3);
}
void test_all_24_are_right_handed(void) {       // determinante +1: sem espelhamento
  for (uint8_t i = 0; i < kMagRemapCount; ++i) TEST_ASSERT_EQUAL_FLOAT(1.0f, remapDeterminant(i));
}
void test_all_24_are_distinct(void) { /* nenhuma permutação repetida */ }
void test_out_of_range_falls_back_to_default(void) {
  float a[3], b[3]; const float in[3] = {1, 2, 3};
  applyMagRemap(99, in, a); applyMagRemap(kMagRemapDefault, in, b);
  TEST_ASSERT_EQUAL_FLOAT_ARRAY(b, a, 3);
}
```

`remapDeterminant(uint8_t)` é um helper do próprio teste.

- [ ] **Step 2: Rode e confirme a falha** — `pio test -d firmware -e native -f test_magremap`
- [ ] **Step 3: Implemente `MagAxisRemap.h`** — gere as 24 permutações próprias (6 permutações × 8 combinações de sinal, ficando só as de determinante +1) como tabela `constexpr`.
- [ ] **Step 4: Rode e confirme que passa**
- [ ] **Step 5: Commit**

```bash
git add firmware/Gaiaball/src/logic/MagAxisRemap.h firmware/test/test_magremap/
git commit -m "feat(imu): right-handed magnetometer axis remap table, host-tested"
```

---

### Task 3: Configuração — `imuChip` (0x34) e `magAxisRemap` (0x36), schema v7

**Files:**
- Modify: `firmware/Gaiaball/src/logic/GaiaConfig.h`, `firmware/Gaiaball/src/logic/TlvCodec.cpp`, `firmware/Gaiaball/src/services/ConfigStore.cpp` (versão do schema)
- Test: `firmware/test/test_config/test_config.cpp` (acrescentar)

**Interfaces:**
- Consumes: `ImuChip` e `kMagRemapDefault` das tarefas 1 e 2.
- Produces: `GaiaConfig::imuChip` (`uint8_t`, padrão `0`) e `GaiaConfig::magAxisRemap` (`uint8_t`, padrão `kMagRemapDefault`); registros `{0x34, Kind::U8, 0, 2}` e `{0x36, Kind::U8, 0, 23}` em `kRegs`.

- [ ] **Step 1: Escreva os testes que falham**

```cpp
void test_default_config_has_no_imu_selected(void) {
  GaiaConfig c; TEST_ASSERT_EQUAL_UINT8(0, c.imuChip);
  TEST_ASSERT_EQUAL_UINT8(kMagRemapDefault, c.magAxisRemap);
}
void test_imu_chip_out_of_range_rejects_whole_write(void) {   // Review Focus 1
  GaiaConfig c; c.imuChip = 1;
  const uint8_t buf[] = {0x34, 1, 3, 0x02, 1, 100};            // 3 inválido + taxa válida
  TEST_ASSERT_FALSE(tlvApply(c, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_UINT8(1, c.imuChip);                       // nada mudou
  TEST_ASSERT_EQUAL_UINT8(50, c.imuNotifyHz);
}
void test_pre_v7_blob_loads_as_unconfigured(void) {            // sem 0x34 no blob
  GaiaConfig c; c.imuChip = 2;
  uint8_t buf[384]; const size_t n = tlvSerializeWithout(c, buf, sizeof(buf), 0x34);
  GaiaConfig loaded; tlvApplyLenient(loaded, buf, n);
  TEST_ASSERT_EQUAL_UINT8(0, loaded.imuChip);
}
void test_roundtrip_preserves_both_new_registers(void) { /* serialize → apply → iguais */ }
```

`tlvSerializeWithout` é um helper do teste que filtra um id do buffer serializado.

- [ ] **Step 2: Rode e confirme a falha** — `pio test -d firmware -e native -f test_config`
- [ ] **Step 3: Implemente** os dois campos, as duas entradas em `kRegs`, e suba `kConfigSchemaVersion` para `7`.
- [ ] **Step 4: Rode e confirme que passa** + suíte inteira
- [ ] **Step 5: Commit**

```bash
git commit -am "feat(config): imuChip (0x34) and magAxisRemap (0x36) registers, NVS schema v7"
```

---

### Task 4: `imuDetected` (0x35) — somente-leitura, fora da tabela do codec

**Files:**
- Modify: `firmware/Gaiaball/src/services/BleService.cpp:323` (`onConfigRead`), `firmware/Gaiaball/src/services/BleService.h`, `firmware/Gaiaball/src/App.cpp`
- Create: `firmware/Gaiaball/src/logic/ImuStatus.h`
- Test: `firmware/test/test_config/test_config.cpp` (acrescentar)

**Interfaces:**
- Produces:
  ```cpp
  namespace gaia {
  // bits 0..1: ImuChip vigente no BOOT; bit 2: sensor encontrado.
  constexpr uint8_t kImuFound = 1u << 2;
  constexpr uint8_t imuStatusByte(ImuChip bootChip, bool found);
  }
  ```
  `BleService` ganha `void setImuStatus(uint8_t)`, chamado uma vez pelo `App` depois de `ImuService::begin`. `onConfigRead` anexa o registro `{0x35, 1, status}` ao buffer de `tlvSerialize` antes de responder.

- [ ] **Step 1: Escreva os testes que falham**

```cpp
void test_status_byte_encodes_boot_selection_and_found(void) {
  TEST_ASSERT_EQUAL_UINT8(0x06, imuStatusByte(ImuChip::Mpu9250, true));   // 2 | kImuFound
  TEST_ASSERT_EQUAL_UINT8(0x01, imuStatusByte(ImuChip::Icm20948, false));
  TEST_ASSERT_EQUAL_UINT8(0x00, imuStatusByte(ImuChip::None, false));
}
void test_0x35_is_rejected_on_write(void) {
  GaiaConfig c; const uint8_t buf[] = {0x35, 1, 0x06};
  TEST_ASSERT_FALSE(tlvApply(c, buf, sizeof(buf)));
}
void test_0x35_never_appears_in_serialized_config(void) {   // logo, nunca vai para a NVS
  GaiaConfig c; uint8_t buf[384]; const size_t n = tlvSerialize(c, buf, sizeof(buf));
  for (size_t i = 0; i < n; ) { TEST_ASSERT_NOT_EQUAL(0x35, buf[i]); i += 2 + buf[i + 1]; }
}
```

- [ ] **Step 2: Rode e confirme a falha**
- [ ] **Step 3: Implemente** `ImuStatus.h`, `BleService::setImuStatus`, e o anexo em `onConfigRead`. O `0x35` **não** entra em `kRegs`.
- [ ] **Step 4: Rode e confirme que passa** + as duas toolchains
- [ ] **Step 5: Commit**

```bash
git commit -am "feat(ble): read-only imuDetected (0x35) carrying boot selection + found bit"
```

---

### Task 5: Interface `ImuDriver`; `Mpu9250` a implementa; remap vem da configuração

**Files:**
- Create: `firmware/Gaiaball/src/services/ImuDriver.h`
- Modify: `firmware/Gaiaball/src/services/Mpu9250.h/.cpp`, `firmware/Gaiaball/src/services/ImuService.h/.cpp`

**Interfaces:**
- Consumes: `applyMagRemap` (tarefa 2), `GaiaConfig::magAxisRemap` (tarefa 3).
- Produces:
  ```cpp
  class ImuDriver {
   public:
    virtual ~ImuDriver() = default;
    virtual bool begin(TwoWire &wire, bool ad0) = 0;
    virtual bool setSampleRateHz(uint8_t hz, uint8_t dlpfMode) = 0;
    virtual bool setGyroFullScale(uint8_t fsSel) = 0;
    virtual bool dataReady() = 0;
    virtual bool readAccelGyro(int16_t acc[3], int16_t gyro[3]) = 0;
    virtual bool readMag(float uT[3]) = 0;          // quadro NATIVO do magnetômetro
    virtual bool magOk() const = 0;
    virtual float gyroLsbPerDps() const = 0;
    virtual float gyroRangeDps() const = 0;
    virtual float dlpfCutoffHz() const = 0;
    virtual uint8_t dlpfMode() const = 0;
    virtual uint8_t whoAmI() const = 0;
    virtual uint32_t errorCount() const = 0;
    static constexpr float kAccelLsbPerG = 8192.0f;  // ±4 g nos dois chips
  };
  ```
  `ImuService` passa a guardar `ImuDriver *imu_` e o remap deixa de ser literal.

- [ ] **Step 1: Escreva o teste que falha**

Em `firmware/test/test_magremap/`, pinando a Review Focus 5 — o quadro publicado na captura usa o remap corrente:

```cpp
void test_capture_publishes_in_the_configured_frame(void) {
  const float native[3] = {1.0f, 2.0f, 3.0f};
  float a[3], b[3];
  applyMagRemap(kMagRemapDefault, native, a);
  applyMagRemap(/* outro índice qualquer */ 0, native, b);
  TEST_ASSERT_FALSE(a[0] == b[0] && a[1] == b[1] && a[2] == b[2]);
}
```

- [ ] **Step 2: Rode e confirme a falha**
- [ ] **Step 3: Extraia a interface.** `Mpu9250 : public ImuDriver`, métodos `override`, `kAccelLsbPerG` sai da classe concreta. `ImuService::mpu_` vira `ImuDriver *imu_` apontando para um `Mpu9250` membro (ainda sem escolha dinâmica — isso é a tarefa 7). Em `ImuService.cpp:314`, trocar o literal `{uT[1], uT[0], -uT[2]}` por `applyMagRemap(active_.magAxisRemap, uT, chipFrame)`, mantendo a ordem atual: publicar a amostra de captura **depois** do remap e antes de `magApply`.
- [ ] **Step 4: Rode e confirme que passa** — comportamento idêntico: `pio test -d firmware -e native` 131+ PASS, as duas toolchains OK
- [ ] **Step 5: Commit**

```bash
git commit -am "refactor(imu): extract ImuDriver interface; magnetometer remap comes from config"
```

---

### Task 6: Driver do ICM-20948

**Files:**
- Create: `firmware/Gaiaball/src/services/Icm20948.h`, `firmware/Gaiaball/src/services/Icm20948.cpp`

**Interfaces:**
- Consumes: `ImuDriver` (tarefa 5), `planFrontEnd`/`dlpfCutoffHz` (tarefa 1).
- Produces: `class Icm20948 : public ImuDriver`.

Não há teste de host possível (vive em `src/services/`, que o ambiente `native` exclui); toda a aritmética testável já saiu para a tarefa 1. A verificação desta tarefa é compilação nas duas toolchains + a bancada da tarefa 9.

- [ ] **Step 1: Implemente `Icm20948`**

Pontos que o datasheet DS-000189 fixa e o driver precisa respeitar:
- `selectBank(n)` escrevendo `REG_BANK_SEL` (0x7F, bits 5:4), chamado **uma vez por operação**, nunca por registro, e sempre devolvendo ao banco 0 ao fim de uma escrita de configuração — leitura de dados no banco errado devolve lixo plausível, que é o modo de falha mais caro aqui.
- `begin`: `WHO_AM_I` (banco 0, 0x00) deve valer `0xEA`; `DEVICE_RESET`; limpar `SLEEP` em `PWR_MGMT_1` (o chip acorda dormindo); `CLKSEL = 1`; `PWR_MGMT_2 = 0x00`; acelerômetro em ±4 g para casar `kAccelLsbPerG`.
- `setSampleRateHz`: `planFrontEnd(ImuChip::Icm20948, hz, dlpfMode)` e escrever `GYRO_SMPLRT_DIV` (banco 2, u8) e `ACCEL_SMPLRT_DIV_1/2` (banco 2, 16 bits), mais `GYRO_DLPFCFG`/`ACCEL_DLPFCFG` com seus bits `FCHOICE`.
- `setGyroFullScale`: `GYRO_FS_SEL` em `GYRO_CONFIG_1` (banco 2), mesma ordem ±250/500/1000/2000 do MPU-9250.
- `dataReady`: `INT_STATUS_1` (banco 0), limpa na leitura.
- `readAccelGyro`: uma leitura de 12 bytes a partir de `ACCEL_XOUT_H` (banco 0, 0x2D) — acelerômetro e giroscópio são contíguos, sem termômetro no meio.
- `readMag`: AK09916 em 0x0C por bypass (`BYPASS_EN` em `INT_PIN_CFG`, com `I2C_MST_EN` desligado em `USER_CTRL`). `WIA1`(0x00)=0x48 **e** `WIA2`(0x01)=0x09 — conferir os dois, porque só o 0x48 não distingue do AK8963. Modo contínuo por `CNTL2` (0x31). Ler `ST1`, os 6 bytes de dados e **`ST2` (0x18)**, obrigatório para encerrar a medição. Escala fixa **0,15 µT/LSB**; não existem registros ASA, então nada do `magAdj_` do `Mpu9250` se aplica. Devolver no **quadro nativo do AK09916** — o remapeamento é do `ImuService`.
- Contar toda falha de transação I²C em `errorCount()`, como o `Mpu9250` faz.

- [ ] **Step 2: Compile nas duas toolchains**

Run: `pio run -d firmware -e esp32dev` e `arduino-cli compile --fqbn esp32:esp32:esp32 firmware/Gaiaball`
Expected: ambos OK, Flash abaixo de 70 %.

- [ ] **Step 3: Rode a suíte nativa** — `pio test -d firmware -e native` segue verde (nada de `logic/` mudou)

- [ ] **Step 4: Commit**

```bash
git add firmware/Gaiaball/src/services/Icm20948.*
git commit -m "feat(imu): register-level ICM-20948 driver (banked registers, AK09916)"
```

---

### Task 7: `ImuService` escolhe o driver pela configuração

**Files:**
- Modify: `firmware/Gaiaball/src/services/ImuService.h/.cpp`, `firmware/Gaiaball/src/App.cpp`

**Interfaces:**
- Consumes: `ImuDriver`, `Icm20948`, `Mpu9250`, `imuStatusByte` (tarefas 4–6).
- Produces: `ImuService::bootChip()` → `ImuChip` e `ImuService::imuOk()` já existente; juntos alimentam `BleService::setImuStatus`.

- [ ] **Step 1: Implemente a seleção**

`begin(ConfigStore &store)` lê `cfg.imuChip` **uma vez** e guarda em `bootChip_` (a troca só vale no próximo boot, por design):
- `None` → nenhuma tentativa de I²C, `imuOk_ = false`, log `I [imu] no IMU selected; configure it in the app`, a fila e a task ociosa continuam existindo (o contrato atual de `begin` já prevê isso).
- `Icm20948` / `Mpu9250` → constrói o driver correspondente e mantém as 10 tentativas a 500 ms.

Os dois drivers são membros concretos da classe (sem alocação dinâmica, como o resto do firmware); `imu_` aponta para um deles.

No `App.cpp`, depois de `imu_.begin(...)`, chamar `ble_.setImuStatus(imuStatusByte(imu_.bootChip(), imu_.imuOk()))`.

- [ ] **Step 2: Verifique a Review Focus 2 na bancada (chip selecionado, sensor ausente)**

Com a bola ICM-20948 e `imuChip = 2` (GY-91) gravado: o boot deve registrar as 10 tentativas, terminar com o IMU desligado, **o BLE continuar anunciando**, e a leitura da configuração devolver `0x35 = 0x02` (seleção 2, bit de encontrado apagado).

- [ ] **Step 3: As duas toolchains + suíte nativa**
- [ ] **Step 4: Commit**

```bash
git commit -am "feat(imu): pick the driver from config; unconfigured means IMU off"
```

---

### Task 8: config-app — campos, presets e rótulos do DLPF

**Files:**
- Modify: `config-app/src/protocol/tlv.ts`, `config-app/src/panels/config-panel.ts`
- Test: `config-app/src/panels/config-panel.test.ts`

**Interfaces:**
- Consumes: ids `0x34`/`0x36` e suas faixas (tarefa 3).
- Produces: `TLV.imuChip = 0x34`, `TLV.imuDetected = 0x35`, `TLV.magAxisRemap = 0x36`; `isPerBall(f: Field): boolean` — o predicado de exclusão de preset, que passa a cobrir PIN, calibração magnética, `imuChip` e `magAxisRemap`.

- [ ] **Step 1: Escreva os testes que falham**

```ts
it('não inclui imuChip nem magAxisRemap num preset salvo', () => { /* Review Focus 3 */ });
it('ignora imuChip ao carregar um preset antigo que o contenha', () => { ... });
it('rotula o DLPF com os cortes do chip selecionado', () => {
  expect(dlpfOptions(2)).toContainEqual([3, '41 Hz']);      // MPU-9250
  expect(dlpfOptions(1)).not.toContainEqual([3, '41 Hz']);  // ICM-20948
});
it('FIELD_DEFAULTS tem entrada para todo Field novo', () => { ... });
it('mostra o aviso de reinício só enquanto a seleção difere da lida do dispositivo', () => { ... });
```

- [ ] **Step 2: Rode e confirme a falha** — `npm test` em `config-app/`

- [ ] **Step 3: Implemente**

- Campo **IMU** (`kind: 'select'`, aba **Básico**), opções `[[0,'não configurado'],[1,'ICM-20948'],[2,'GY-91 (MPU-9250)']]`, com `info` explicando o chip, o padrão de fábrica sem IMU e a exigência de reinício.
- **Aviso de reinício visível** (não no tooltip): ao mudar a seleção de IMU, aparece ao lado do campo uma faixa dizendo que a troca só vale depois de reiniciar, com um botão **"Reiniciar agora"** que dispara o comando `0x13` com a mesma confirmação que o botão do painel de conexão usa (`connection-panel.ts:9`). Não precisa de "salve primeiro": Aplicar já envia o `0x11` depois de conferir a releitura. A faixa some quando a seleção volta ao valor lido do dispositivo.
- Campo **Eixos do magnetômetro** (`kind: 'select'`, aba **Avançado**), 24 opções rotuladas como `"+Y +X −Z"`, com `info` avisando que **mudar invalida a calibração magnética** e que a ordem correta é remap primeiro, calibrar depois. O mesmo aviso de reinício **não** se aplica aqui: o remap é lido a cada ciclo pelo `applyConfig`, então vale na hora.
- `Field.options` passa a aceitar `(cfg: Map<number, unknown>) => [number, string][]` além do array estático, para os rótulos do DLPF seguirem `imuChip`.
- `isPerBall` substitui o `hasPin` nos dois laços de preset (`psave` e `pload`).
- `FIELD_DEFAULTS` ganha `[TLV.imuChip, 0]` e `[TLV.magAxisRemap, <padrão>]`.

- [ ] **Step 4: Rode e confirme que passa** — `npm test` 52+ PASS, `npm run build` OK
- [ ] **Step 5: Commit**

```bash
git commit -am "feat(app): IMU selection and magnetometer axis fields; keep both out of presets"
```

---

### Task 9: config-app — leitura ao conectar e linha de estado do IMU

**Files:**
- Modify: `config-app/src/main.ts`, `config-app/src/panels/config-panel.ts`, `config-app/src/panels/metrics-panel.ts`
- Test: `config-app/src/panels/config-panel.test.ts`

**Interfaces:**
- Consumes: `TLV.imuDetected` (tarefa 8), o byte de estado da tarefa 4.
- Produces: `imuStatusText(detected: number | undefined, selected: number): string`.

- [ ] **Step 1: Escreva o teste que falha**

```ts
it.each([
  [undefined, 0, 'não configurado'],
  [0x06, 2, 'GY-91 (MPU-9250) ok'],
  [0x01, 1, 'ICM-20948 selecionado, não encontrado'],
  [0x05, 2, 'trocado, aguardando reinício'],   // boot=ICM+found, seleção atual=GY-91
])('descreve o estado do IMU', (detected, selected, want) => {
  expect(imuStatusText(detected, selected)).toBe(want);
});
```

- [ ] **Step 2: Rode e confirme a falha**
- [ ] **Step 3: Implemente** `imuStatusText`, a linha no painel Métricas (com `data-info`, como as demais), e a leitura automática da configuração ao conectar em `main.ts` — populando o snapshot do painel de config para que a guarda "Leia a configuração antes de aplicar" fique satisfeita sem clique.
- [ ] **Step 4: Rode e confirme que passa** — `npm test`, `npm run build`
- [ ] **Step 5: Commit**

```bash
git commit -am "feat(app): read config on connect; show IMU state in the metrics panel"
```

---

### Task 10: Documentação

**Files:**
- Modify: `firmware/docs/hardware-pinout.md:41-50`, `firmware/docs/protocol.md`, `firmware/README.md`, `docs/bench-validation.md`, `README.md`

- [ ] **Step 1: Corrija `hardware-pinout.md`** — o texto atual afirma GY-91 "confirmado pelo usuário". Passa a registrar as duas variantes, a evidência de 2026-11-12 (WHO_AM_I `0x48` lido em `0x75`, que é registro reservado no ICM-20948) e que a bola existente é `GY-ICM20948V2`.
- [ ] **Step 2: `protocol.md`** — registros `0x34`/`0x35`/`0x36`, `0x35` marcado somente-leitura e ausente da NVS, e as linhas que hoje dizem "MPU-9250" como se fosse o único chip (`0x02`, `0x05`, `0x06`, `0x0C`).
- [ ] **Step 3: `firmware/README.md`** — a cadeia de processamento é a mesma para os dois chips; tabela de diferenças (bancos, divisores, DLPF, magnetômetro).
- [ ] **Step 4: `docs/bench-validation.md`** — seleção do IMU como passo anterior a qualquer teste de orientação; ordem remap-antes-de-calibrar; e **o caminho de gravação por usbipd-win**, que ainda não está documentado: `usbipd bind --busid <X-Y>` (admin, uma vez), `usbipd attach --wsl --busid <X-Y>` (a cada replug), regras udev do PlatformIO para `/dev/ttyUSB0` nascer acessível.
- [ ] **Step 5: Commit**

```bash
git commit -am "docs: two IMU variants, new registers, usbipd-win flashing path"
```
