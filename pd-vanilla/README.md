# Gaiaball no Pd vanilla (sem objetos compilados)

`gaiaball-vanilla.pd` recebe os dados da Gaiaball no Pure Data **sem nenhum external**: só
objetos que já vêm com o Pd vanilla. Serve quando o objeto `[gaiaball]` (que precisa ser
compilado para cada sistema) não está disponível, por exemplo num Mac sem o binário.

```
Gaiaball ──Bluetooth──▶ config-app (Chrome/Edge) ──WebSocket ws://127.0.0.1:9478──▶ gaiaball-vanilla.pd ──▶ [r gaia.*]
```

## Como usar

1. Abra `gaiaball-vanilla.pd` no Pd (vanilla **0.51 ou mais novo**; macOS, Windows ou Linux).
2. Abra o config-app (<https://gaiasenses.github.io/gaiaball/>) no Chrome ou no Edge e
   conecte a bola. A chave **Enviar ao Pd** já vem ligada. Em até 2 s o quadrado
   **conectado** do patch acende, e o config-app mostra "Pd: conectado · N pacotes/s".
   - Se o Chrome pedir permissão para acessar **apps neste dispositivo**, permita. Se o
     config-app mostrar "Pd: bloqueado pelo Chrome", libere em Configurações do site.
3. Nos seus patches, use `[r gaia.acc.x]`, `[r gaia.co2]` etc. Os nomes são os mesmos do
   objeto `[gaiaball]`, então os patches feitos para ele funcionam sem mudar nada. Não use
   os dois ao mesmo tempo: eles disputam a mesma porta.

Para usar outra porta, coloque o patch na mesma pasta do seu patch e crie
`[gaiaball-vanilla 9500]`. Depois mude a porta no **Avançado** do config-app. Usado como
objeto, a saída dele dá o estado (`connected 0|1`, `rate N`, `gap N`). Esse mesmo estado
também chega em `[r gaiaball.status]`.

## Os dados

| Nome | Valor | Quando |
|---|---|---|
| `gaia.acc.x`, `gaia.acc.y`, `gaia.acc.z` | aceleração em mg | a cada pacote de movimento (~50 Hz) |
| `gaia.quat` | lista `w x y z` | idem |
| `gaia.sensors` | lista `roll pitch yaw accX accY accZ co2` (graus, mg, ppm) | idem; co2 = 0 até chegar o primeiro CO₂ |
| `gaia.mic.rms`, `gaia.mic.peak` | lista `L R` em dBFS | idem, só se o áudio da bola estiver funcionando |
| `gaia.breath` | 0/1 (sopro reconhecido) | idem |
| `gaia.co2` | ppm | a cada pacote de CO₂ |
| `gaia.co2.trigger` | 0/1 (passou do limiar) | idem |
| `gaia.lat`, `gaia.lon` | graus | a cada pacote de navegação, se a navegação estiver ligada |
| `gaia.speed` | graus/s | idem |
| `gaia.bearing` | graus (convenção do Mapbox) | idem |
| `gaia.settled` | 0/1 (globo parado) | idem |

Quando a conexão cai (aba fechada ou 2 s sem pacotes), `gaia.breath` e
`gaia.co2.trigger` recebem 0 uma vez. Assim nenhum 1 fica preso no patch.

## Como funciona (para quem for mexer)

- `[pd rede]`: `[netreceive -b]` escuta só em 127.0.0.1. Atende um cliente por vez: se um
  segundo conectar, os dois caem e o config-app reconecta sozinho em 2 s.
- `[pd http]` + `[pd sha1]`: o aperto de mão do WebSocket. O SHA-1 é calculado com `[expr]`.
  Como o Pd guarda os números em float de 32 bits, cada palavra de 32 bits vira duas metades
  de 16 bits. Roda uma vez por conexão.
- `[pd websocket]`: lê os quadros byte a byte, numa máquina de estados dentro de um único
  `[expr]`. Também responde a ping e a close.
- `[pd pacotes]`: decodifica os pacotes 0x01, 0x02 e 0x07 como `pd-gaiaball/src/gb_decode.c`
  (protocolo em `firmware/docs/protocol.md`).
- `[pd estado]`: `connected`, `rate` e `gap`.

O `.pd` é gerado por `tools/gerar_patch.py`. Para mudar o patch, edite o gerador e rode
`python3 tools/gerar_patch.py`. Editar o `.pd` direto também funciona, mas a mudança se
perde se o gerador for rodado de novo.

## Teste

```sh
node pd-vanilla/tests/e2e.mjs --pd /caminho/do/pd
```

O teste abre o Pd sem interface e faz o aperto de mão como o Chrome. Ele confere:

- a chave da RFC 6455 e mais 40 chaves aleatórias, contra o `node:crypto`;
- os pacotes de referência do protocolo e os ângulos de Euler, contra as fórmulas do
  decodificador em C;
- pedido HTTP e quadros partidos em vários pedaços, quadro com tamanho de 16 bits;
- ping, close, quadro sem máscara, pedido sem chave e dois clientes ao mesmo tempo;
- silêncio de 2 s e o ritmo de 100 pacotes/s.
