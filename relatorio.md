# Relatório Técnico — Análise e Recomendações de Melhoria do Firmware Gaia

Arquivo analisado `firmware-original.ino`
Plataforma ESP32 com Arduino Framework
Data da análise 05112026
Objetivo Melhorar estabilidade, confiabilidade, desempenho e comunicação entre o ESP32 e a aplicação web.

---

## 1. Resumo executivo

O firmware Gaia integra diferentes periféricos e subsistemas em um único ESP32

- IMU ICM-20948, utilizando DMP e FIFO.
- Dois microfones INMP441, utilizando I²S estéreo.
- Sensor de CO₂ SC8, utilizando UART.
- Potenciômetro para ajuste de sensibilidade.
- LEDs de sinalização e controle.
- Comunicação BLE com uma aplicação executada no navegador.

A implementação atual apresenta uma estrutura funcional, mas concentra aquisição, processamento, controle de eventos e transmissão BLE em um único `loop()`.

Essa arquitetura cria dependências entre tarefas que possuem necessidades temporais diferentes. A aquisição da IMU, por exemplo, pode ser prejudicada por atrasos introduzidos pela comunicação BLE ou pela escrita serial.

Os principais pontos que merecem correção são

1. Transmissão BLE baseada em JSON, com possível limitação de tamanho dos pacotes.
2. Transmissão repetida de dados sem verificar se houve uma nova amostra.
3. Bloqueios e atrasos artificiais no `loop()`.
4. Ausência de separação entre aquisição, processamento e comunicação.
5. Leitura da FIFO da IMU sem estratégia explícita de drenagem e monitoramento de overflow.
6. Processamento de áudio baseado em média de valores absolutos, sem cálculo RMS.
7. Filtro de áudio excessivamente lento.
8. Parser UART do SC8 sem recuperação robusta de sincronização.
9. Conflito de GPIO entre o LED de comando BLE e o LED de detecção sonora.
10. Ausência de métricas operacionais para diagnosticar falhas e medir desempenho.

A recomendação principal é evoluir gradualmente para uma arquitetura baseada em tarefas independentes, buffers de dados e protocolo binário de comunicação.

---

# 2. Arquitetura atual

## 2.1. Visão geral

Atualmente, todos os subsistemas são inicializados no `setup()` e executados de forma cooperativa dentro do `loop()`.

A arquitetura lógica pode ser representada da seguinte forma

```text
                         ESP32
                           
          +----------------+----------------+
                                          
          v                v                v
      ICM-20948         INMP441 x2          SC8 CO2
       I2CDMP             I2S               UART
                                          
          v                v                v
    FIFO  Quaternion   Nível de áudio    CO2 PPM
                                          
          v                v                
    Euler  Accel      Baseline  IIR       
                                          
                          v                
                    Detecção de pico       
                                          
                          v                
                    Evento CO2 virtual     
                                          
          +----------------+----------------+
                           
                           v
                      loop() principal
                           
                 +---------+---------+
                                    
                 v                   v
             JSON IMU            JSON CO2
                                    
                 v                   v
             BLE Notify          BLE Notify
                                    
                 +---------+---------+
                           
                           v
                    Aplicação Web
```

O potenciômetro é lido pelo ADC1 e controla o limiar de detecção de áudio. Os LEDs são controlados pelas rotinas de detecção, pelos valores de CO₂ e pelos comandos BLE.

## 2.2. Componentes e responsabilidades

 Subsistema                 Hardware  interface  Responsabilidade                         
 -------------------------  --------------------  ---------------------------------------- 
 Orientação                 ICM-20948  I²C       Quaternion, orientação e aceleração      
 Processamento IMU          DMP  FIFO            Processamento interno dos dados da IMU   
 Áudio                      2 × INMP441  I²S     Aquisição de áudio estéreo               
 Detecção sonora            Software              Baseline, filtro e detecção de picos     
 CO₂                        SC8  UART            Leitura da concentração em PPM           
 Controle de sensibilidade  Potenciômetro  ADC1  Ajuste do limiar de detecção             
 Sinalização                LEDs GPIO             Indicação visual de eventos e estados    
 Comunicação                BLE  GATT            Envio de dados e recebimento de comandos 
 Aplicação cliente          Browser               Recepção, visualização e controle        

## 2.3. Configuração atual dos periféricos

### ICM-20948

- Interface I²C.
- SDA GPIO 21.
- SCL GPIO 22.
- Clock I²C 400 kHz.
- Endereço configurado determinado por `AD0_VAL = 0`.
- Processamento DMP.
- Sensores habilitados orientação e acelerômetro.
- Buffer FIFO.

### Microfones INMP441

- Interface I²S.
- WS GPIO 25.
- SCK GPIO 26.
- SD GPIO 33.
- Frequência de amostragem 16 kHz.
- Formato 32 bits.
- Canais estéreo.
- DMA quatro buffers de 256 frames.

### Sensor SC8

- Interface UART1.
- RX ESP32 GPIO 16.
- TX ESP32 GPIO 17.
- Baud rate 9600.
- Formato 8N1.
- Frame 16 bytes.
- Cabeçalho `0x42 0x4D`.
- Concentração de CO₂ bytes 6 e 7.
- Validação checksum.

### Potenciômetro

- GPIO 34.
- Interface ADC1.
- Resolução 12 bits.
- Faixa de limiar configurada 10.000 a 500.000.

### BLE

- Biblioteca atual ESP32 BLE Arduino.
- Serviço GATT UUID `19b10000-e8f2-537e-4f6c-d104768a1214`.
- Características
  - Sensor `19b10001-e8f2-537e-4f6c-d104768a1214`.
  - LED `19b10002-e8f2-537e-4f6c-d104768a1214`.
  - CO₂ `19b10003-e8f2-537e-4f6c-d104768a1214`.

Importante esses UUIDs devem ser preservados durante a primeira etapa de refatoração, para evitar incompatibilidade com a aplicação web existente.

---

# 3. Pontos de falha e recomendações

## 3.1. Comunicação BLE



Prioridade CRÍTICA

### Situação atual

O firmware utiliza JSON para transmitir os dados

```cpp
serializeJson(doc, output);
pSensorCharacteristic-setValue(value.c_str());
pSensorCharacteristic-notify();
```

O mesmo procedimento é utilizado para a característica de CO₂.

### Problemas identificados

1. Overhead de JSON

A transmissão inclui nomes de propriedades, delimitadores e representação textual de números.

Isso aumenta o volume de dados e o processamento necessário no ESP32 e no navegador.

2. Limitação de tamanho das notificações

O tamanho máximo de uma notificação ATT depende do MTU negociado. Com o MTU padrão de 23 bytes, o payload máximo é de 20 bytes.

Os documentos JSON utilizados pelo firmware podem ultrapassar esse limite.

Não existe no código uma estratégia explícita de negociação de MTU, fragmentação ou validação do tamanho efetivamente transmitido.

3. Alocações dinâmicas

A cada ciclo são criados objetos `String` e documentos JSON.

O uso repetitivo de alocação dinâmica pode contribuir para fragmentação de heap e instabilidade em aplicações que permanecem ligadas por longos períodos.

4. Notificações sem dados novos

As características são notificadas a cada execução do bloco BLE, mesmo quando não existe uma nova amostra da IMU ou do CO₂.

Isso pode produzir mensagens vazias, repetidas ou sem atualização efetiva.

5. Ausência de métricas de comunicação

O firmware não registra

- Quantidade de notificações enviadas.
- Taxa efetiva de transmissão.
- Falhas de envio.
- Intervalos entre notificações.
- Número de sequência.
- Perda de pacotes percebida pelo cliente.

### Recomendações

A. Adotar protocolo binário

Substituir o JSON por estruturas binárias compactas.

Exemplo conceitual

```cpp
struct __attribute__((packed)) IMUPacket {
    uint32_t timestamp_us;
    uint16_t sequence;

    int16_t quat_w;
    int16_t quat_x;
    int16_t quat_y;
    int16_t quat_z;

    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
};
```

As escalas de conversão devem ser documentadas e fixadas no protocolo.

O navegador poderá utilizar `DataView` para decodificar os dados.

B. Separar características por finalidade

Manter

- Característica de IMU.
- Característica de CO₂.
- Característica de comandos.

O envio de cada conjunto deve ocorrer de acordo com sua própria frequência e disponibilidade de dados.

C. Implementar controle de MTU

Negociar um MTU maior quando suportado pelos dois dispositivos.

Não depender exclusivamente disso o protocolo deverá ter tamanho máximo conhecido e prever fragmentação ou pacotes menores quando necessário.

D. Avaliar NimBLE-Arduino

Considerar a migração da biblioteca BLE atual para NimBLE-Arduino, visando reduzir consumo de memória e melhorar o controle da pilha BLE.

Essa migração deve ser testada com o navegador e com os dispositivos efetivamente utilizados.

E. Implementar reconexão e estado de conexão

Garantir que o ESP32 retorne ao advertising após desconexão e que a aplicação web consiga detectar e recuperar a conexão.

---

## 3.2. Bloqueios e temporização do loop principal

Prioridade CRÍTICA

### Situação atual

O `loop()` concentra

- Leitura do potenciômetro.
- Aquisição de áudio.
- Processamento de eventos.
- Leitura do SC8.
- Leitura da FIFO da IMU.
- Cálculo de quaternion e Euler.
- Serialização JSON.
- Transmissão BLE.
- Logs seriais.

Também existem atrasos explícitos

```cpp
delay(12);
```

E

```cpp
if (myICM.status != ICM_20948_Stat_FIFOMoreDataAvail) {
    if (!haveQuat) delay(2);
    delay(10);
}
```

### Problemas identificados

Os atrasos introduzem variações na frequência de execução e podem prejudicar a aquisição de sensores que precisam ser atendidos regularmente.

A transmissão BLE pode atrasar a leitura da IMU.

A escrita serial também pode bloquear a execução caso o buffer esteja cheio.

A aquisição de áudio ocorre a cada 20 ms, embora o periférico produza dados continuamente a 16 kHz.

### Recomendações

- Remover delays artificiais como mecanismo de controle da frequência.
- Utilizar agendamento por timestamps.
- Separar aquisição e transmissão.
- Utilizar buffers intermediários.
- Evitar processamento desnecessário no caminho crítico.
- Medir o tempo de execução de cada tarefa.

O controle temporal deve utilizar `millis()` ou `micros()` e mecanismos de sincronização apropriados, em vez de depender de atrasos fixos.

---

## 3.3. Arquitetura de tarefas e FreeRTOS

Prioridade CRÍTICA

### Situação atual

Todos os subsistemas executam sequencialmente no mesmo `loop()`.

Isso simplifica o desenvolvimento inicial, mas cria dependências entre processos com requisitos temporais diferentes.

### Problemas identificados

Uma operação de comunicação pode atrasar a aquisição dos sensores.

Uma falha de um subsistema pode prejudicar os demais.

Não existe isolamento claro entre aquisição, processamento e transmissão.

### Recomendações

Adotar uma arquitetura baseada em tarefas independentes do FreeRTOS.

Proposta

 Tarefa            Responsabilidade                           Prioridade sugerida 
 ----------------  -----------------------------------------  ------------------- 
 IMU Task          Leitura da FIFO e aquisição de orientação  Alta                
 Audio Task        Leitura I²S e cálculo RMS                  Alta                
 UART Task         Parser e atualização do SC8                Média               
 Processing Task   Eventos, filtros e lógica de controle      Média               
 BLE Task          Serialização e transmissão dos dados       Média               
 Control Task      Potenciômetro e LEDs                       Baixa               
 Diagnostics Task  Estatísticas e monitoramento               Baixa               

As prioridades definitivas devem ser definidas após medições de latência, ocupação de CPU e comportamento real dos periféricos.

### Comunicação entre tarefas

Utilizar

- Filas FreeRTOS para eventos e comandos.
- Mutexes quando houver compartilhamento de estruturas.
- Buffers circulares para aquisição contínua.
- Variáveis atômicas ou mecanismos de sincronização para estados compartilhados.

Evitar que callbacks BLE manipulem diretamente estruturas complexas utilizadas por outras tarefas.

---

## 3.4. Processamento da IMU ICM-20948

Prioridade ALTA

### Situação atual

O firmware utiliza o DMP para obter orientação e acelerômetro.

A leitura da FIFO é realizada por

```cpp
myICM.readDMPdataFromFIFO(&data);
```

O código processa o pacote retornado, mas não implementa uma estratégia explícita de drenagem da FIFO até que não existam mais pacotes disponíveis.

### Problemas identificados

1. Possível acúmulo na FIFO

Se a aquisição ficar temporariamente atrasada, os dados podem acumular.

Isso aumenta a latência e pode provocar overflow.

2. Reconstrução do quaternion

O cálculo de `q0` utiliza

```cpp
double q0 = sqrt(1.0 - ((q1q1) + (q2q2) + (q3q3)));
```

Uma pequena imprecisão numérica pode tornar o argumento negativo, produzindo `NaN`.

3. Ausência de normalização

Não existe normalização explícita do quaternion completo antes de seu uso.

4. Inicialização infinita

O firmware tenta inicializar a IMU indefinidamente

```cpp
while (!initialized) {
    myICM.begin(WIRE_PORT, AD0_VAL);
}
```

Se o sensor estiver desconectado ou com falha, o restante do sistema nunca será inicializado.

5. Configuração das taxas do DMP

A configuração

```cpp
myICM.setDMPODRrate(DMP_ODR_Reg_Quat9, 1);
myICM.setDMPODRrate(DMP_ODR_Reg_Accel, 1);
```

precisa ser revisada com base na documentação da versão da biblioteca SparkFun utilizada.

O comentário `Set to 1Hz` não deve ser considerado uma confirmação da frequência efetiva.

### Recomendações

- Drenar a FIFO enquanto existirem pacotes pendentes, respeitando um limite de processamento por ciclo.
- Detectar e registrar overflow.
- Normalizar o quaternion.
- Proteger a reconstrução de `q0` contra valores inválidos.
- Definir explicitamente a frequência desejada.
- Medir a frequência real de aquisição.
- Implementar timeout de inicialização e recuperação do I²C.
- Evitar enviar dados antigos como se fossem amostras novas.

Exemplo de proteção numérica

```cpp
double q0_squared =
    1.0 - (q1q1 + q2q2 + q3q3);

double q0 = sqrt(fmax(0.0, q0_squared));
```

Depois da reconstrução, o quaternion completo deve ser normalizado.

A transformação dos eixos utilizada no cálculo de Euler também deve ser documentada e validada fisicamente.

---

## 3.5. Processamento dos microfones INMP441

Prioridade ALTA

### Situação atual

Os dois microfones são lidos pelo I²S em modo estéreo.

A função `lerNivelAudio()` calcula a média dos valores absolutos dos dois canais e retorna o maior resultado.

O processamento é realizado a cada 20 ms.

### Problemas identificados

1. Média absoluta em vez de RMS

A média absoluta pode funcionar como estimativa simples de amplitude, mas o RMS representa melhor a energia do sinal.

2. Ausência de remoção de offset DC

O cálculo atual não estima nem remove explicitamente o componente DC.

3. Filtro excessivamente lento

O firmware utiliza

```cpp
#define MIC_IIR_ALPHA 0.99f
```

Com atualização a 50 Hz, esse filtro possui resposta muito lenta.

O tempo característico de resposta fica próximo de dois segundos.

Isso pode atrasar significativamente a detecção de picos.

4. Falta de validação da inicialização I²S

Se `i2s_driver_install()` ou `i2s_set_pin()` falhar, o firmware registra o erro, mas continua executando como se o periférico estivesse disponível.

5. Possível perda de dados

A leitura periódica pode não acompanhar corretamente a produção contínua de amostras.

Também não existe monitoramento de overflow ou de erros do DMA.

6. Ausência de métricas individuais

Os níveis esquerdo e direito são descartados após a escolha do maior valor.

Isso dificulta diagnosticar diferenças entre os microfones.

### Recomendações

A. Utilizar RMS

Implementar

[
RMS = sqrt{frac{1}{N}sum_{i=1}^{N}x_i^2}
]

Calcular o RMS individual dos dois canais.

B. Manter canais separados

Armazenar

- RMS esquerdo.
- RMS direito.
- RMS máximo.
- Baseline de cada canal.

A lógica atual de disparo pode continuar utilizando o maior nível, mas a aquisição deve preservar os dois canais.

C. Revisar o filtro

Testar valores de alpha entre 0,70 e 0,90, conforme a resposta desejada.

Uma solução mais completa utiliza

- Filtro rápido para detecção.
- Filtro lento para estimativa de ruído ambiente.
- Histerese para evitar disparos repetidos.

D. Melhorar a aquisição I²S

Avaliar leitura contínua por tarefa dedicada e utilização adequada dos buffers DMA.

Monitorar erros de leitura e overflow.

E. Melhorar a inicialização

Se o I²S falhar, registrar o subsistema como indisponível e impedir leituras inválidas.

A falha do áudio não precisa impedir o funcionamento da IMU ou do BLE.

---

## 3.6. Comunicação UART com o SC8

Prioridade ALTA

### Situação atual

O firmware utiliza UART1, 9600 baud, 8N1, e lê frames de 16 bytes.

A interpretação dos dados considera

- Cabeçalho `0x42 0x4D`.
- CO₂ nos bytes 6 e 7.
- Checksum no byte 15.

Essa interpretação corresponde ao protocolo considerado para o SC8 utilizado no projeto.

### Problemas identificados

1. Sincronização de frame

O código pressupõe que os primeiros 16 bytes disponíveis correspondem exatamente ao início de um frame.

Se houver desalinhamento, os próximos frames podem ser descartados.

2. Ausência de recuperação de sincronização

Não existe uma máquina de estados para procurar o cabeçalho e recuperar a leitura após bytes inválidos.

3. Frequência de leitura

A função é chamada a cada 500 ms, enquanto a UART pode receber dados continuamente.

O código não registra pacotes acumulados, inválidos ou perdidos.

4. Falta de indicador de validade

Se o sensor deixar de transmitir, o último valor válido pode permanecer indefinidamente como se estivesse atualizado.

### Recomendações

Implementar um parser incremental

1. Procurar `0x42`.
2. Verificar `0x4D`.
3. Acumular os 16 bytes.
4. Validar o checksum.
5. Atualizar a concentração de CO₂ somente após validação.
6. Recuperar a sincronização em caso de erro.

Adicionar

- Timestamp da última leitura válida.
- Contador de frames recebidos.
- Contador de frames inválidos.
- Contador de erros de checksum.
- Estado de disponibilidade do sensor.

A leitura física do SC8 deve ocorrer na frequência real de atualização do sensor, sem tentar produzir amostras artificiais em frequências superiores.

---

## 3.7. Controle de LEDs e GPIOs

Prioridade ALTA

### Situação atual

O código declara

```cpp
const int ledPin = 2;
#define LED_SOM_PIN 2
```

O mesmo GPIO2 é utilizado para

- Comandos recebidos pela característica BLE.
- Indicação de detecção sonora.
- Indicação do evento virtual de CO₂.

### Problemas identificados

As diferentes rotinas podem sobrescrever umas às outras.

Um comando BLE pode apagar o LED enquanto o evento de áudio ainda deveria mantê-lo aceso.

Também existe uma questão de hardware GPIO2 é um pino de strapping no ESP32 clássico, devendo ser utilizado com cuidado durante a inicialização.

### Recomendações

- Separar os GPIOs de cada função, quando o hardware permitir.
- Criar uma camada centralizada de controle de LEDs.
- Definir prioridades para os estados de sinalização.
- Não permitir que callbacks BLE alterem diretamente LEDs controlados por eventos independentes.

Exemplo de organização

```text
LED Controller
    
    +-- Estado do comando BLE
    
    +-- Estado da detecção sonora
    
    +-- Estado do CO2
    
    +-- Estado de erro
```

A saída física deve ser determinada por uma única função de controle.

---

## 3.8. Potenciômetro e ADC

Prioridade MÉDIA

### Situação atual

O potenciômetro utiliza ADC1 no GPIO34.

A leitura ocorre a cada 100 ms.

### Pontos de melhoria

- Aplicar filtragem simples nas leituras do ADC.
- Validar os valores extremos.
- Considerar calibração e não linearidade do ADC.
- Implementar uma pequena histerese no ajuste do limiar, se houver instabilidade.
- Remover leituras redundantes `potRaw` e `lerPotenciometro()` fazem duas leituras separadas.

Também é importante verificar se a faixa de tensão do potenciômetro é compatível com a entrada do ESP32.

---

## 3.9. Logs e diagnóstico

Prioridade MÉDIA

### Situação atual

O firmware imprime informações diretamente pela serial.

A aceleração é impressa a cada pacote recebido

```cpp
SERIAL_PORT.print(F(Accel X));
```

### Problemas identificados

- Logs frequentes podem bloquear o loop.
- Não existe controle de nível de log.
- Não existe painel consolidado de métricas.
- É difícil identificar se uma falha é causada por BLE, IMU, áudio ou UART.

### Recomendações

Criar um módulo de diagnóstico com métricas periódicas

 Métrica                   Finalidade                     
 ------------------------  ------------------------------ 
 IMU Hz                    Frequência efetiva da IMU      
 BLE packetss             Taxa de transmissão            
 BLE disconnects           Quantidade de desconexões      
 BLE send failures         Falhas de envio                
 FIFO overflow             Perda de continuidade da IMU   
 I²S errors                Falhas na aquisição de áudio   
 UART checksum errors      Integridade do SC8             
 CO₂ last update           Idade da última leitura válida 
 Free heap                 Memória disponível             
 Minimum free heap         Menor memória disponível       
 Task stack watermark      Monitoramento de pilha         
 Looptask execution time  Custo de processamento         

Os logs devem ser configuráveis e executados em intervalos controlados.

---

# 4. Arquitetura recomendada

## 4.1. Visão geral

A arquitetura proposta separa aquisição, processamento e comunicação.

```text
                         ESP32
                           
       +-------------------+-------------------+
                                             
       v                   v                   v
    IMU Task            Audio Task           UART Task
                                             
       v                   v                   v
   ICM FIFO             I2S DMA              SC8 Parser
                                             
       v                   v                   v
 Quaternion             RMS  Peak           CO2 PPM
                                             
       +-------------------+-------------------+
                           
                           v
                   Processing Task
                           
              +------------+------------+
                                       
              v                         v
         Event Manager              Data State
                                       
              v                         v
         LED Controller           Shared Buffers
                                        
                                        v
                                    BLE Task
                                        
                                        v
                                  Binary Protocol
                                        
                                        v
                                  Browser  Web App
```

## 4.2. Separação das responsabilidades

### IMU Task

Responsável exclusivamente por

- Consultar a FIFO.
- Ler pacotes disponíveis.
- Validar dados.
- Normalizar quaternion.
- Atualizar timestamp e sequência.
- Publicar amostras no buffer.

### Audio Task

Responsável por

- Ler o DMA I²S.
- Processar canais esquerdo e direito.
- Remover offset DC.
- Calcular RMS.
- Atualizar baseline e filtros.
- Publicar eventos de pico.

### UART Task

Responsável por

- Receber bytes continuamente.
- Reconstruir frames.
- Validar checksum.
- Atualizar valor de CO₂.
- Controlar timeout e disponibilidade.

### Processing Task

Responsável por

- Processar eventos de áudio.
- Controlar eventos virtuais de CO₂.
- Atualizar estados lógicos.
- Controlar o LED Controller.

### BLE Task

Responsável por

- Monitorar conexão.
- Consumir dados dos buffers.
- Montar pacotes binários.
- Transmitir notificações.
- Controlar sequência e estatísticas.

### Control Task

Responsável por

- Ler potenciômetro.
- Atualizar limiar.
- Processar comandos recebidos.
- Encaminhar comandos ao controlador apropriado.

### Diagnostics Task

Responsável por

- Coletar métricas.
- Monitorar heap.
- Monitorar tarefas.
- Registrar falhas.
- Disponibilizar informações para diagnóstico.

---

# 5. Protocolo de comunicação recomendado

## 5.1. Manter compatibilidade durante a migração

A primeira versão da refatoração deve preservar

- Nome BLE `Globo_1`.
- UUID do serviço.
- UUID das três características.
- Semântica dos comandos LED.
- Campos e unidades utilizados pela aplicação web.

A substituição do JSON por binário deve ser feita em uma etapa controlada, com atualização correspondente do frontend.

## 5.2. Estrutura dos pacotes

Recomenda-se utilizar pacotes binários compactos com

- Identificador de tipo.
- Versão do protocolo.
- Número de sequência.
- Timestamp.
- Dados do sensor.

Exemplo conceitual

```cpp
struct PacketHeader {
    uint8_t packetType;
    uint8_t protocolVersion;
    uint16_t sequence;
    uint32_t timestamp_us;
};
```

Os pacotes específicos devem conter apenas os campos necessários.

Exemplos

- IMU quaternion e aceleração.
- CO₂ PPM e estado de validade.
- Áudio RMS dos canais e evento de pico.
- Diagnóstico estado dos periféricos e métricas.

É preferível manter os pacotes de IMU e CO₂ independentes, pois possuem frequências de atualização diferentes.

## 5.3. Frequências de atualização

As frequências devem ser definidas de acordo com a capacidade real dos sensores e a necessidade da aplicação.

 Subsistema         Frequência de referência  Observação                         
 --------------  --------------------------  ---------------------------------- 
 IMU                100 Hz como meta inicial  Confirmar ODR e desempenho do DMP  
 Áudio                   16 kHz de aquisição  Processamento por blocos           
 RMS  detecção            50 Hz ou superior  Ajustar conforme latência desejada 
 SC8             Frequência nativa do sensor  Não inventar novas amostras        
 Potenciômetro                         10 Hz  Suficiente para ajuste manual      
 BLE IMU              Até 100 notificaçõess  Validar MTU, conexão e perdas      
 BLE CO₂          Sob atualização ou demanda  Independente da IMU                

Esses valores são metas de projeto, não garantias de desempenho.

A frequência efetiva deve ser medida no ESP32 e no navegador.

---

# 6. Confiabilidade e recuperação de falhas

A nova arquitetura deverá permitir que cada subsistema falhe de forma isolada.

Exemplos

 Falha              Comportamento recomendado                                                   
 -----------------  --------------------------------------------------------------------------- 
 IMU desconectada   Tentar reinicialização sem interromper BLE                                  
 FIFO overflow      Registrar evento e recuperar FIFO                                           
 I²S indisponível   Desativar detecção sonora e manter os demais sensores                       
 SC8 sem resposta   Preservar último valor, marcando-o como desatualizado                       
 BLE desconectado   Continuar adquirindo dados e voltar ao advertising                          
 BLE congestionado  Descartar dados antigos de visualização, preservando a amostra mais recente 
 Heap baixo         Registrar alerta e evitar novas alocações desnecessárias                    
 Erro de tarefa     Registrar estado e aplicar estratégia de recuperação                        

Também é recomendável implementar watchdog e monitoramento de stack das tarefas.

O watchdog não deve ser utilizado para mascarar bloqueios recorrentes primeiro devem ser corrigidas as causas dos bloqueios.

---

# 7. Plano de implementação

Recomenda-se dividir a refatoração em etapas para reduzir o risco de interromper o funcionamento atual.

## Fase 1 — Correções críticas sem mudança de arquitetura

- [ ] Corrigir conflito entre GPIO2 e LED de comando BLE.
- [ ] Remover notificações BLE sem dados novos.
- [ ] Separar atualização da IMU e do CO₂.
- [ ] Corrigir proteção numérica do quaternion.
- [ ] Validar inicialização I²S e DMP.
- [ ] Implementar parser incremental do SC8.
- [ ] Remover logs seriais excessivos.
- [ ] Revisar atrasos no loop.

Resultado esperado firmware mais previsível, preservando a estrutura atual e a compatibilidade BLE.

## Fase 2 — Otimização da comunicação

- [ ] Medir o tamanho atual dos pacotes JSON.
- [ ] Validar o MTU negociado.
- [ ] Medir taxa de notificações e perdas.
- [ ] Definir protocolo binário versionado.
- [ ] Implementar decoder no frontend.
- [ ] Adicionar número de sequência e timestamp.
- [ ] Implementar tratamento de reconexão.

Resultado esperado menor overhead e comunicação mais previsível.

## Fase 3 — Refatoração para FreeRTOS

- [ ] Criar IMU Task.
- [ ] Criar Audio Task.
- [ ] Criar UART Task.
- [ ] Criar Processing Task.
- [ ] Criar BLE Task.
- [ ] Criar Control Task.
- [ ] Criar Diagnostics Task.
- [ ] Definir filas, buffers e mecanismos de sincronização.

Resultado esperado isolamento entre aquisição, processamento e comunicação.

## Fase 4 — Melhoria da qualidade dos sensores

- [ ] Implementar RMS estéreo.
- [ ] Remover offset DC.
- [ ] Revisar filtro IIR.
- [ ] Implementar histerese.
- [ ] Validar ODR do DMP.
- [ ] Monitorar FIFO.
- [ ] Validar orientação e transformação dos eixos.
- [ ] Calibrar limiares e resposta dos microfones.

Resultado esperado aquisição mais confiável e detecção sonora mais responsiva.

## Fase 5 — Testes de estabilidade

- [ ] Testar operação contínua por 8 horas.
- [ ] Testar operação contínua por 24 horas.
- [ ] Testar desconexão e reconexão BLE.
- [ ] Testar interrupção e retorno do SC8.
- [ ] Testar inicialização com IMU desconectada.
- [ ] Testar falha de inicialização I²S.
- [ ] Medir heap e stack durante operação.
- [ ] Medir frequência e jitter da IMU.
- [ ] Medir latência entre aquisição e navegador.
- [ ] Verificar ausência de perda progressiva de memória.

Resultado esperado comprovação quantitativa da estabilidade do sistema.

---

# 8. Critérios de aceitação

A refatoração deverá ser considerada satisfatória quando atender aos seguintes critérios

 Critério               Meta                                                       
 ---------------------  ---------------------------------------------------------- 
 Aquisição da IMU       100 Hz sustentados, se suportados pela configuração do DMP 
 Transmissão BLE        Sem truncamento de pacotes                                 
 Sequência de pacotes   Perdas detectáveis pelo cliente                            
 Latência               Medida e documentada, sem assumir valores não testados     
 CO₂                    Dados atualizados somente após frame válido                
 Áudio                  RMS independente para os dois canais                       
 Detecção sonora        Resposta calibrada e sem disparos espúrios recorrentes     
 Reconexão BLE          Recuperação automática                                     
 Falhas de periféricos  Isolamento e recuperação                                   
 Memória                Sem crescimento progressivo durante teste prolongado       
 Estabilidade           Operação contínua por 24 horas sem travamento              

Os limites de jitter, latência e perda de pacotes deverão ser definidos após a medição do comportamento da versão inicial.

---

# 9. Recomendações finais

A arquitetura atual é adequada como protótipo funcional, mas precisa evoluir para suportar operação contínua e aquisição de múltiplos sensores sem interferência entre subsistemas.

As três mudanças de maior impacto são

1. Separar aquisição e transmissão a leitura dos sensores não pode depender do tempo necessário para enviar dados via BLE.
2. Otimizar o protocolo BLE utilizar pacotes binários compactos, timestamps e números de sequência.
3. Implementar tarefas independentes utilizar FreeRTOS para isolar IMU, áudio, UART, processamento e comunicação.

A recomendação é não reescrever todo o firmware de uma vez. Primeiro devem ser corrigidos os problemas de temporização, BLE, UART e GPIO, mantendo a interface atual. Depois, a migração para tarefas independentes e protocolo binário poderá ocorrer de forma incremental.

Diretriz central do projeto a aquisição dos sensores deve ser contínua e independente da conexão BLE. A comunicação deve transmitir dados disponíveis, sem bloquear ou controlar o ritmo de aquisição.

Essa separação será fundamental para que o Gaia consiga operar de maneira estável, com baixa latência e possibilidade de expansão futura, incluindo transmissão por Wi-FiWebSocket sem precisar reestruturar a lógica dos sensores.
