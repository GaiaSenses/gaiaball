# Gaiaball

Firmware, ferramenta de configuração e documentação do globo interativo Gaiaball
(ESP32 + IMU ICM-20948 + microfones INMP441 + sensor de CO₂, transmitindo por BLE para o
site Gaiasenses).

## Mapa do projeto

| Pasta / arquivo | O que é |
|---|---|
| `firmware-original.ino` | Firmware legado (v1), **preservado como referência**; não é mais compilado. |
| `firmware/` | Firmware v2 (ESP32, NimBLE, protocolo binário). Ver `firmware/README.md`, `firmware/docs/protocol.md` e `firmware/docs/architecture.md`. |
| `config-app/` | App web (Vite + TypeScript, Web Bluetooth) para configurar e testar o v2. Ver `config-app/README.md`. |
| `relatorio/` | Relatório interativo com os defeitos do legado, as correções do v2 e o checklist de bancada: abra `relatorio/index.html` no navegador. |
| `relatorio.md` | Relatório inicial em texto. |
| `docs/` | Documentação: [`docs/bench-validation.md`](docs/bench-validation.md) (roteiro de bancada) e `docs/superpowers/` (spec e plano). |

## Quickstart

Firmware (a partir da raiz):

```sh
pio test -d firmware -e native                 # testes no host (66)
pio run -d firmware -e esp32dev -t upload      # compila e grava
pio device monitor -d firmware -e esp32dev     # serial, 115200
```

Alternativa com arduino-cli:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/Gaiaball
```

Config-app:

```sh
cd config-app
npm install
npm run dev        # http://localhost:5173 (Chrome/Edge); ?demo=1 roda sem dispositivo
npm test           # vitest
npm run build
```

Validação em hardware: siga [`docs/bench-validation.md`](docs/bench-validation.md) e o
checklist do relatório interativo (`relatorio/index.html`, seção 7).

## Compatibilidade com o Gaiasenses-web (ordem de deploy)

O site (repositório `Gaiasenses-web`) precisa do branch **`feature/gaiaball-binary-protocol`**,
que adiciona o decodificador do protocolo binário v2. Esse branch deve ser mergeado **junto
com** a gravação do firmware v2.

- **Site novo + firmware velho: funciona.** O site tem fallback para o JSON do legado, então
  a ordem é flexível nesse sentido.
- **Firmware novo + site velho: NÃO funciona.** O site antigo só entende JSON e não decodifica
  os pacotes binários do v2.

Portanto: faça o deploy do site primeiro (ou ao mesmo tempo) e só então grave o firmware v2
nos globos em campo.
