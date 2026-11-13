import type { GaiaballClient, StatusDetail } from '../ble/gaiaball-client.ts';
import { PresetStore } from '../presets/preset-store.ts';
import { decodeNavRef, encodeNavRef, encodeTlv, navRefToString, parseNavRefString, TLV, type TlvWrite } from '../protocol/tlv.ts';
import { infoDot } from './info.ts';

type Kind = 'number' | 'range' | 'checkbox' | 'select' | 'text' | 'pin' | 'navref';
export interface Field {
  id: number;
  label: string;
  kind: Kind;
  /** Didactic tooltip (the "i" dot): what the parameter does + its unit. */
  info: string;
  min?: number;
  max?: number;
  /** Static list, or a function of the configuration currently in the form (e.g. DLPF labels follow imuChip). */
  options?: [number, string][] | ((cfg: Map<number, unknown>) => [number, string][]);
  /** Per-filter on/off toggle: the register value that means "filter OFF". */
  offValue?: number;
  /** Value restored in the input when the toggle is turned on with nothing sensible. */
  onDefault?: number;
  /** Hides the row while false; re-evaluated when navMode changes and after every populate. */
  visible?: (cfg: Map<number, unknown>) => boolean;
  /**
   * Wire units per displayed unit (×10 registers are shown in °/s with one decimal). The form
   * shows value/scale; getValue converts back. min/max stay in wire units.
   */
  scale?: number;
}
interface Group { title: string; fields: Field[] }

export const resolveOptions = (f: Field, cfg: Map<number, unknown>): [number, string][] =>
  typeof f.options === 'function' ? f.options(cfg) : (f.options ?? []);

/**
 * DLPF labels per chip. The register value is the same index on both chips, but the cut-offs
 * differ: MPU-9250 DLPF_CFG 1..7 = 184/92/41/20/10/5/3600 Hz; ICM-20948 GYRO_DLPFCFG 1..7 =
 * 151.8/119.5/51.2/23.9/11.6/5.7/361.4 Hz. Index 7 (3600 / 361.4 Hz) is not offered: against a
 * 10-200 Hz output it is no filter at all, only aliasing. The ICM accel cut-offs differ slightly from its
 * gyro ones (the firmware writes the same index to both): these are the gyro values.
 */
export function dlpfOptions(chip: number): [number, string][] {
  const auto: [number, string] = [0, 'automático'];
  if (chip === 1) return [auto, [1, '151,8 Hz'], [2, '119,5 Hz'], [3, '51,2 Hz'], [4, '23,9 Hz'], [5, '11,6 Hz'], [6, '5,7 Hz']];
  if (chip === 2) return [auto, [1, '184 Hz'], [2, '92 Hz'], [3, '41 Hz'], [4, '20 Hz'], [5, '10 Hz'], [6, '5 Hz']];
  // IMU not selected: the cut-off depends on the chip, so only the index is honest.
  return [auto, ...[1, 2, 3, 4, 5, 6].map((i): [number, string] => [i, `índice ${i} (depende do IMU)`])];
}

/**
 * Labels of the 24 magnetometer axis mappings, "+Y +X -Z" meaning out.x = +in.y, out.y = +in.x,
 * out.z = -in.z. AUTHORITY: firmware/Gaiaball/src/logic/MagAxisRemap.cpp (kMagRemapTable). The
 * order and labels here were copied from it and nothing tests the two against each other, so
 * change them only together.
 */
const MAG_AXIS_LABELS = [
  '+X +Y +Z', '+X -Y -Z', '-X +Y -Z', '-X -Y +Z', '+X +Z -Y', '+X -Z +Y', '-X +Z +Y', '-X -Z -Y', '+Y +X -Z', '+Y -X +Z', '-Y +X +Z', '-Y -X -Z', '+Y +Z +X', '+Y -Z -X', '-Y +Z -X', '-Y -Z +X', '+Z +X +Y', '+Z -X -Y', '-Z +X -Y', '-Z -X +Y', '+Z +Y -X', '+Z -Y +X', '-Z +Y +X', '-Z -Y -X',
];
const MAG_AXIS_OPTIONS: [number, string][] = MAG_AXIS_LABELS.map((l, i) => [i, l]);

/** Registers that describe one physical ball and must never travel in a preset. */
export const isPerBall = (f: Field): boolean =>
  f.id === TLV.pin || f.id === TLV.magCal || f.id === TLV.imuChip || f.id === TLV.magAxisRemap || f.id === TLV.navRef;

/** Display names of the IMU choices (index = TLV 0x34 value). */
const IMU_NAMES = ['não configurado', 'ICM-20948', 'GY-91 (MPU-9250)'];

/**
 * True while a reboot is needed for the IMU choice to take effect. The reference is the chip in
 * force at boot (imuDetected bits 0-1) when known, because after Aplicar the stored value already
 * equals the selection yet the running firmware still drives the old chip; before the first
 * imuDetected it falls back to the value read from the device.
 */
export function imuRebootPending(selected: number, readChip: number | undefined, detected: number | undefined): boolean {
  const ref = detected !== undefined ? detected & 0x03 : readChip;
  return ref !== undefined && selected !== ref;
}

/** One-line IMU state from TLV 0x35 (bits 0-1 chip at boot, bit 2 found, bit 3 magnetometer found) and the live selection. */
export function imuStatusText(detected: number | undefined, selected: number): string {
  if (detected === undefined) return selected === 0 ? 'não configurado' : `${IMU_NAMES[selected] ?? '?'} selecionado`;
  const boot = detected & 0x03, found = (detected & 0x04) !== 0, mag = (detected & 0x08) !== 0;
  if (boot !== selected) return 'trocado, aguardando reinício';
  if (boot === 0) return 'não configurado';
  if (!found) return `${IMU_NAMES[boot]} selecionado, não encontrado`;
  return mag ? `${IMU_NAMES[boot]} ok` : `${IMU_NAMES[boot]} ok (sem magnetômetro)`;
}

const isJoystick = (cfg: Map<number, unknown>): boolean => Number(cfg.get(TLV.navMode)) === 2;

const GROUPS: Group[] = [
  { title: 'Aquisição', fields: [
    { id: TLV.orientationMode, label: 'Modo', kind: 'select', options: [[0, 'Quat6 (relativo)'], [1, 'Quat9 (absoluto)']],
      info: 'Como o firmware calcula a orientação: Quat6 usa giroscópio + acelerômetro (referência relativa, imune a ímãs); Quat9 acrescenta o magnetômetro (yaw absoluto, porém sensível a interferência magnética — e só confiável depois de calibrar o magnetômetro). Sem unidade.' },
    { id: TLV.imuChip, label: 'IMU', kind: 'select', options: [[0, 'não configurado'], [1, 'ICM-20948'], [2, 'GY-91 (MPU-9250)']],
      info: 'Qual chip de movimento está soldado nesta bola. O mesmo firmware serve aos dois (ICM-20948 ou GY-91/MPU-9250), mas ele não adivinha: a bola sai de fábrica com o IMU DESLIGADO (“não configurado”) de propósito, para que ninguém dependa de um sensor que ninguém escolheu. Trocar o chip só vale depois de REINICIAR a bola (o barramento é inicializado uma vez, no boot). Sem unidade.' },
    { id: TLV.imuNotifyHz, label: 'Taxa (Hz)', kind: 'number', min: 10, max: 200,
      info: 'Quantas amostras de orientação a bola envia por segundo via BLE; também reconfigura a amostragem da IMU. Unidade: hertz (Hz), de 10 a 200.' },
    { id: TLV.gyroFsSel, label: 'Faixa do giroscópio', kind: 'select',
      options: [[0, '±250 °/s'], [1, '±500 °/s'], [2, '±1000 °/s'], [3, '±2000 °/s']],
      info: 'Fundo de escala do giroscópio. Mais estreito mede mais fino (±250 → 0,0076 °/s por bit; ±2000 → 0,061 °/s) e essa resolução é integrada direto na deriva da orientação. Estreito demais satura: uma bola girada na mão passa fácil de 500 °/s e a fusão precisa reiniciar. O giroscópio não vai no fio, então mudar a faixa não afeta nenhum cliente. Seleção, sem unidade.' },
    { id: TLV.imuDlpfMode, label: 'Filtro anti-serrilhamento (DLPF)', kind: 'select',
      options: (cfg) => dlpfOptions(Number(cfg.get(TLV.imuChip) ?? 0)),
      info: 'Passa-baixas interno do IMU (os cortes mostrados dependem do chip escolhido em “IMU”; no ICM-20948 o acelerômetro tem cortes ligeiramente diferentes, e os rótulos são os do giroscópio), aplicado ANTES da decimação para a taxa de saída. A decimação não faz média: o que passar acima da metade da taxa volta dobrado para dentro da banda (aliasing) e depois é indistinguível de movimento real — é assim que vibração entra como tremor lento. “Automático” escolhe o corte mais largo que ainda fica abaixo da metade da taxa (a 50 Hz → 20 Hz no MPU-9250 e 11,6 Hz no ICM-20948, cujos cortes ficam um índice acima do limite e por isso caem um índice mais estreito). O índice 7 (sem filtro útil) não é oferecido. Unidade: hertz (Hz).' },
  ] },
  { title: 'Fusão (AHRS)', fields: [
    { id: TLV.magAxisRemap, label: 'Eixos do magnetômetro', kind: 'select', options: MAG_AXIS_OPTIONS,
      info: 'Como os eixos do magnetômetro são rearranjados para coincidir com os do acelerômetro/giroscópio (o magnetômetro tem eixos próprios, diferentes em cada chip). “+Y +X -Z” significa x = +Y lido, y = +X lido, z = -Z lido. Vale na hora, sem reiniciar. ATENÇÃO: mudar isto INVALIDA a calibração magnética existente — o firmware publica as amostras de captura já remapeadas e o ajuste hard/soft-iron do navegador é feito sobre elas. Ordem correta na bancada: primeiro o remap, depois calibrar. Sem unidade.' },
    { id: TLV.fusionGainX100, label: 'Ganho da fusão (×100)', kind: 'number', min: 0, max: 500,
      info: 'Peso do acelerômetro/magnetômetro contra o giroscópio — na prática, a frequência de corte do filtro complementar. Ganho baixo confia no giroscópio (saída lisa, mais deriva); ganho alto corrige rápido (mais ruído e mais erro durante aceleração). 50 = 0,50, o padrão do algoritmo. Adimensional (×100).' },
    { id: TLV.accelRejectionDeg, label: 'Rejeição de aceleração (°)', kind: 'number', min: 0, max: 90,
      offValue: 0, onDefault: 10,
      info: 'Se a inclinação indicada pelo acelerômetro discordar da estimada por mais que este ângulo, o acelerômetro é ignorado naquela amostra. É o que impede uma bola arremessada, rolada ou sacudida de “tombar” a orientação: durante a aceleração o sensor aponta para a resultante, não para baixo. 0 desliga. Unidade: graus (°); 10° serve para quase tudo.' },
    { id: TLV.magRejectionDeg, label: 'Rejeição magnética (°)', kind: 'number', min: 0, max: 90,
      offValue: 0, onDefault: 10,
      info: 'Mesmo mecanismo aplicado ao rumo: discordância acima deste ângulo faz o magnetômetro ser ignorado, blindando a orientação de um ímã ou celular que passa perto. Só age no modo Quat9. 0 desliga. Unidade: graus (°).' },
    { id: TLV.rejectionTimeoutS, label: 'Tempo até recuperação (s)', kind: 'number', min: 0, max: 60,
      offValue: 0, onDefault: 5,
      info: 'Se a discordância persistir por mais que este tempo, o firmware conclui que o errado é a estimativa (e não o sensor) e volta a usá-lo. Sem isso, uma perturbação longa deixaria a bola só com o giroscópio, derivando livremente. 0 desliga as duas rejeições. Unidade: segundos (s).' },
  ] },
  { title: 'Bias do giroscópio', fields: [
    { id: TLV.biasRestThDpsX100, label: 'Limiar de repouso (×100 °/s)', kind: 'number', min: 1, max: 2000,
      info: 'Quanto a taxa pode oscilar em torno da própria média para a bola contar como parada. O teste é feito na OSCILAÇÃO, não no valor absoluto — por isso funciona mesmo quando o offset do giroscópio é grande, que é justamente quando mais se precisa dele. 200 = 2,00 °/s. Unidade: °/s ×100.' },
    { id: TLV.biasMaxDpsX100, label: 'Offset máximo plausível (×100 °/s)', kind: 'number', min: 1, max: 5000,
      info: 'Até quanto o firmware aceita como offset de fábrica. Serve de trava na estimativa e também impede que uma rotação lenta e constante seja aprendida como offset. O datasheet do MPU-9250 admite ±5 °/s, então 1000 (= 10,00 °/s) deixa margem. Unidade: °/s ×100.' },
    { id: TLV.biasTauS, label: 'Constante de tempo (s)', kind: 'number', min: 1, max: 60,
      info: 'Rapidez com que a estimativa converge enquanto a bola está parada: maior = mais estável e mais lento. Com 4 s a estimativa se assenta em cerca de 15 s de repouso. Unidade: segundos (s).' },
  ] },
  { title: 'Suavização da saída', fields: [
    { id: TLV.quatSmoothMode, label: 'Modo', kind: 'select',
      options: [[0, 'desligada'], [1, 'slerp fixo (legado)'], [2, 'filtro 1€ (adaptativo)']],
      info: 'Como a orientação é suavizada antes de ir para o fio. O “slerp fixo” atrasa tudo igualmente (no máximo ~190 ms a 50 Hz, perceptível como resposta elástica). O “filtro 1€” muda a frequência de corte conforme a velocidade angular: filtra forte com a bola quase parada e quase nada durante um movimento rápido. Sem unidade.' },
    { id: TLV.slerpAmount, label: 'Slerp fixo: intensidade', kind: 'range', min: 0, max: 100,
      offValue: 0, onDefault: 20,
      info: 'Intensidade do slerp de um polo, usada apenas no modo “slerp fixo”: 0 = sem filtro, 100 = máximo (≈190 ms de atraso a 50 Hz). Escala 0–100, sem unidade.' },
    { id: TLV.oneEuroFcMinX100, label: '1€: corte mínimo (×100 Hz)', kind: 'number', min: 1, max: 5000,
      info: 'Frequência de corte com a bola parada: menor = menos tremor, mais atraso enquanto nada se move. 100 = 1,00 Hz. Unidade: Hz ×100 (só no modo filtro 1€).' },
    { id: TLV.oneEuroBetaX1000, label: '1€: beta (×1000)', kind: 'number', min: 0, max: 10000,
      info: 'Quanto o corte sobe por cada °/s de velocidade angular: maior = menos atraso no movimento rápido. 20 = 0,020 Hz por °/s. Adimensional ×1000 (só no modo filtro 1€).' },
    { id: TLV.accelLpAlphaX1000, label: 'Aceleração: filtro EMA (×1000)', kind: 'number', min: 0, max: 1000,
      offValue: 0, onDefault: 300,
      info: 'Média móvel exponencial da aceleração publicada (não entra na fusão, que sempre usa a leitura crua). Valor = alfa ×1000: 1000 = sem filtro; menor = mais liso e mais lento. Adimensional (0–1000).' },
    { id: TLV.accelHampelKX10, label: 'Aceleração: filtro de espícula (k ×10)', kind: 'number', min: 0, max: 100,
      offValue: 0, onDefault: 30,
      info: 'Compara cada amostra nova com a mediana das 5 últimas e a substitui pela mediana se ela se afastar mais de k desvios robustos (MAD). Remove leitura corrompida do I²C sem adicionar atraso nenhum — ao contrário da EMA, que espalharia a espícula pelas amostras seguintes. 0 desliga; 30 = k 3,0. Adimensional ×10.' },
  ] },
  { title: 'Áudio', fields: [
    { id: TLV.audioEnabled, label: 'Habilitado', kind: 'checkbox',
      info: 'Liga a captura dos dois microfones I²S e o detector de sopro. Liga/desliga, sem unidade.' },
    { id: TLV.audioFastAlphaX1000, label: 'Alfa rápido (×1000)', kind: 'number', min: 0, max: 1000,
      info: 'Velocidade do envelope rápido do detector de sopro (segue o pico). Valor = alfa EMA ×1000: maior = resposta mais rápida. Adimensional.' },
    { id: TLV.audioSlowAlphaX1000, label: 'Alfa lento (×1000)', kind: 'number', min: 0, max: 1000,
      info: 'Velocidade da linha de base (ruído ambiente) do detector; deve ser bem menor que o alfa rápido. Valor = alfa EMA ×1000. Adimensional.' },
    { id: TLV.debounceMs, label: 'Debounce (ms)', kind: 'number', min: 0, max: 65535,
      info: 'Intervalo mínimo entre dois sopros reconhecidos, para o mesmo sopro não disparar duas vezes. Unidade: milissegundos (ms).' },
    { id: TLV.potControlsThreshold, label: 'Pot controla limiar', kind: 'checkbox',
      info: 'Ligado, o potenciômetro físico da bola ajusta o limiar do detector de sopro em tempo real; desligado, vale o “Limiar fixo”. Liga/desliga, sem unidade.' },
    { id: TLV.thresholdFixed, label: 'Limiar fixo', kind: 'number', min: 0, max: 4294967295,
      info: 'Nível acima do qual um sopro é reconhecido, usado quando o potenciômetro não controla. Unidade: contagens RMS do áudio (adimensional).' },
    { id: TLV.potEmaAlphaX1000, label: 'Suavização do potenciômetro (EMA ×1000)', kind: 'number', min: 1, max: 1000,
      offValue: 1000, onDefault: 300,
      info: 'Média móvel exponencial da leitura do potenciômetro (tira o chiado do ADC). Valor = alfa ×1000: 1000 = sem filtro. Adimensional.' },
    { id: TLV.audioBpfEnabled, label: 'Filtro passa-banda (sopro)', kind: 'checkbox',
      info: 'Mantém só a faixa de frequências típica do sopro (entre o high-pass e o low-pass) antes do detector. Ajuste os cortes olhando o espectro no painel Sensores. Liga/desliga, sem unidade.' },
    { id: TLV.audioHpHz, label: 'High-pass (Hz)', kind: 'number', min: 1, max: 7000,
      info: 'Corte inferior do passa-banda: atenua o que fica abaixo dele (vibração, manuseio da bola). Unidade: hertz (Hz).' },
    { id: TLV.audioHpQX100, label: 'Q do high-pass (×100)', kind: 'number', min: 10, max: 2000,
      info: 'Fator de qualidade do corte ×100 (71 = 0,71 = resposta plana). Acima disso cria pico ressonante de ~20·log₁₀(Q) dB no corte; a inclinação vem da ordem do filtro, não do Q. Adimensional.' },
    { id: TLV.audioLpHz, label: 'Low-pass (Hz)', kind: 'number', min: 1, max: 7000,
      info: 'Corte superior do passa-banda: atenua o que fica acima dele. Precisa ficar acima do high-pass. Unidade: hertz (Hz).' },
    { id: TLV.audioLpQX100, label: 'Q do low-pass (×100)', kind: 'number', min: 10, max: 2000,
      info: 'Fator de qualidade do corte ×100 (71 = 0,71 = resposta plana); valores altos criam pico ressonante no corte. Adimensional.' },
    { id: TLV.audioRawStream, label: 'Stream de áudio cru (2 kHz)', kind: 'select',
      options: [[0, 'desligado'], [1, 'ligado']],
      info: 'Envia o áudio mono decimado a 2 kHz para desenhar o espectro no painel Sensores. Ligue só para ajustar os cortes do passa-banda: enquanto está ligado ele consome ~4,3 kB/s e ~45 ms de tempo de ar por segundo — mais que todos os outros pacotes somados. O RMS e o pico não dependem dele (vêm medidos do firmware em cada pacote de orientação). Liga/desliga, sem unidade.' },
  ] },
  { title: 'CO₂', fields: [
    { id: TLV.co2Enabled, label: 'Habilitado', kind: 'checkbox',
      info: 'Liga a leitura do sensor SC8 na UART (o firmware apenas escuta — nunca transmite, pois qualquer consulta emudece o sensor). Liga/desliga, sem unidade.' },
    { id: TLV.co2StaleTimeoutMs, label: 'Timeout de dado velho (ms)', kind: 'number', min: 0, max: 65535,
      info: 'Sem quadro novo do sensor por esse tempo, a leitura é marcada como desatualizada (stale). Unidade: milissegundos (ms).' },
    { id: TLV.virtualPeakPpm, label: 'Pico virtual (ppm)', kind: 'number', min: 0, max: 65535,
      info: 'Valor de CO₂ simulado enviado ao site quando um sopro é detectado — dispara a composição sem esperar a resposta lenta do sensor real (T90 < 3 min). Unidade: ppm.' },
    { id: TLV.virtualDelayMs, label: 'Atraso virtual (ms)', kind: 'number', min: 0, max: 65535,
      info: 'Espera entre o sopro detectado e o início do pulso virtual de CO₂. Unidade: milissegundos (ms).' },
    { id: TLV.virtualHoldMs, label: 'Duração virtual (ms)', kind: 'number', min: 0, max: 65535,
      info: 'Tempo que o pulso virtual permanece ativo antes de voltar à leitura real. Unidade: milissegundos (ms).' },
    { id: TLV.co2EmaAlphaX1000, label: 'Suavização (EMA ×1000)', kind: 'number', min: 1, max: 1000,
      offValue: 1000, onDefault: 300,
      info: 'Média móvel exponencial das leituras reais de CO₂. Valor = alfa ×1000: 1000 = sem filtro; menor = mais liso, porém mais atraso. Adimensional.' },
    { id: TLV.co2TriggerPpm, label: 'Gatilho de CO₂ (ppm)', kind: 'number', min: 400, max: 10000,
      info: 'Concentração de CO₂ a partir da qual a bola marca o gatilho no pacote de CO₂ (o site usa esse bit para disparar a composição, sem precisar conhecer o limiar). O ar de uma sala fica perto de 400–800 ppm; um sopro perto do sensor passa de 2000 ppm. Vale na hora. Unidade: partes por milhão (ppm), de 400 a 10000.' },
    { id: TLV.co2TriggerHystPpm, label: 'Histerese do gatilho (ppm)', kind: 'number', min: 0, max: 2000,
      info: 'Folga para o gatilho desligar: depois de disparar, ele só volta a zero quando o CO₂ cai abaixo do gatilho MENOS este valor. Evita que uma leitura oscilando em torno do limiar fique ligando e desligando. 0 = desliga assim que cair abaixo do gatilho. Unidade: ppm, de 0 a 2000.' },
    { id: TLV.co2RawLogMode, label: 'Log bruto', kind: 'checkbox',
      info: 'Imprime no log serial cada quadro recebido do sensor, em hexadecimal — só para diagnóstico de bancada; desligue depois. Liga/desliga, sem unidade.' },
  ] },
  { title: 'Navegação do globo', fields: [
    { id: TLV.navMode, label: 'Modo de navegação', kind: 'select', options: [[0, 'Desligado'], [1, 'Espelho'], [2, 'Joystick']],
      info: 'Como a rotação da bola move o globo no site. ESPELHO: a rotação da bola é refletida exatamente no globo, sem zona morta nem trava de norte (girou 30° na mão, o globo girou 30°). JOYSTICK: inclinar ou girar a bola a partir do zero faz o globo girar continuamente naquela direção, com velocidade que cresce com o desvio; devolvendo a bola ao zero, o globo para. DESLIGADO: a bola não move o globo. Mudar o modo (ou o ponto de referência, ou zerar a orientação) leva o globo de volta ao ponto de referência; os demais parâmetros de navegação valem na hora, sem reiniciar. Seleção, sem unidade.' },
    { id: TLV.joyMaxSpeedDegS, label: 'Velocidade máxima (°/s)', kind: 'number', min: 1, max: 720, visible: isJoystick,
      info: 'Só no modo Joystick. Velocidade com que o globo gira quando a bola está inclinada pelo “ângulo da velocidade máxima” ou mais. Maior = o globo corre mais rápido com a bola bem inclinada. Unidade: graus por segundo (°/s), de 1 a 720.' },
    { id: TLV.joyFullAngleDeg, label: 'Ângulo da velocidade máxima (°)', kind: 'number', min: 5, max: 90, visible: isJoystick,
      info: 'Só no modo Joystick. Quanto a bola precisa estar afastada do zero para o globo atingir a velocidade máxima; abaixo disso a velocidade cresce gradualmente. Menor = mais sensível (basta inclinar um pouco). Unidade: graus (°), de 5 a 90.' },
    { id: TLV.joyCurveX10, label: 'Curva de resposta (p)', kind: 'number', min: 10, max: 40, scale: 10, visible: isJoystick,
      info: 'Só no modo Joystick. Forma como a velocidade cresce com o desvio: velocidade = máxima × (desvio ÷ ângulo da velocidade máxima)^p. Com p = 1,0 a resposta é linear; com p = 3,0 o começo é bem suave (inclinações pequenas quase não movem o globo, as grandes aceleram forte), bom para ajuste fino. Adimensional, de 1,0 a 4,0.' },
    { id: TLV.navNotifyHz, label: 'Taxa de envio (Hz)', kind: 'number', min: 10, max: 200,
      info: 'Quantas posições do globo (latitude, longitude e rumo) a bola envia por segundo via BLE. Mais alto = movimento mais fluido no site, mais tráfego Bluetooth. Unidade: hertz (Hz), de 10 a 200.' },
    { id: TLV.navRef, label: 'Ponto de referência (lat, lon)', kind: 'navref',
      info: 'Lugar do mundo mostrado no centro da tela quando a bola está na posição zero (norte para cima). O padrão é Campinas/SP (−22,905556; −47,060833). Digite latitude (−90 a 90, negativa = sul) e longitude (−180 a 180, negativa = oeste) em graus, com até 6 casas decimais. Mudar o ponto de referência leva o globo para lá. Não entra nos presets, por ser da instalação desta bola. Unidade: graus (°).' },
    { id: TLV.navStartDegS, label: 'Espelho: velocidade de partida (°/s)', kind: 'number', min: 1, max: 200,
      info: 'No modo Espelho, velocidade angular do globo a partir da qual ele passa a ser considerado “em movimento”. Precisa ser MAIOR que a velocidade de parada. Vale na hora. Unidade: graus por segundo (°/s), de 1 a 200.' },
    { id: TLV.navStopDegS, label: 'Espelho: velocidade de parada (°/s)', kind: 'number', min: 1, max: 100,
      info: 'No modo Espelho, velocidade abaixo da qual o globo começa a contar como parando; precisa durar o “tempo para considerar parado”. Precisa ser MENOR que a de partida (a diferença evita que o estado fique alternando). Vale na hora. Unidade: °/s, de 1 a 100.' },
    { id: TLV.navStopMs, label: 'Tempo para considerar parado (ms)', kind: 'number', min: 50, max: 5000,
      info: 'Quanto tempo a velocidade precisa ficar abaixo do limiar de parada para o globo ser declarado “parado” (usado nos dois modos). Maior = menos falsos “parado” durante um gesto lento. Vale na hora. Unidade: milissegundos (ms), de 50 a 5000.' },
    { id: TLV.joyStartDegSX10, label: 'Joystick: velocidade de partida (°/s)', kind: 'number', min: 1, max: 2000, scale: 10,
      info: 'No modo Joystick, velocidade COMANDADA (a que o desvio da bola pede) a partir da qual o globo passa a se mover. Precisa ser MAIOR que a de parada. Vale na hora. Unidade: °/s, de 0,1 a 200,0 (um decimal).' },
    { id: TLV.joyStopDegSX10, label: 'Joystick: velocidade de parada (°/s)', kind: 'number', min: 1, max: 2000, scale: 10,
      info: 'No modo Joystick, velocidade comandada abaixo da qual o globo começa a contar como parando (depois do “tempo para considerar parado”). Precisa ser MENOR que a de partida. Vale na hora. Unidade: °/s, de 0,1 a 200,0 (um decimal).' },
  ] },
  // No LEDs group on purpose: the LED GPIOs (sound = D2/GPIO2, CO2 = D1/GPIO4)
  // follow the board, and the CO2 alert threshold stays on the firmware default.
  // The TLV registers (0x30/0x31/0x32) still exist and read-back records for ids
  // without a UI field are skipped by populate().
  { title: 'Sistema', fields: [
    { id: TLV.serialLogEnabled, label: 'Log serial (USB)', kind: 'checkbox',
      info: 'Mensagens de log pela USB. Desligado por padrão: formatar texto rouba CPU do caminho crítico de aquisição. Liga/desliga, sem unidade.' },
    { id: TLV.bleName, label: 'Nome BLE (≤16)', kind: 'text',
      info: 'Nome anunciado via Bluetooth (1 a 16 bytes). O site encontra a bola pelo UUID do serviço, então mudar o nome não quebra nada.' },
    { id: TLV.pin, label: 'Novo PIN (4 dígitos)', kind: 'pin',
      info: 'Troca o PIN que desbloqueia comandos e escritas sensíveis. Somente escrita: nunca é lido do dispositivo. Exatamente 4 dígitos.' },
  ] },
];
const FIELDS = GROUPS.flatMap((g) => g.fields);
const byId = (id: number): Field => FIELDS.find((f) => f.id === id)!;

/**
 * Fields shown in the main tab: what someone changes while OPERATING the globe.
 * Everything else is signal-chain tuning and lives under "Avançado", so the panel a
 * person meets first is short enough to read. Both tabs stay in the DOM, so a value
 * tuned under Avançado is still collected and applied.
 */
const BASIC_IDS: ReadonlySet<number> = new Set([
  TLV.orientationMode, TLV.imuChip, TLV.imuNotifyHz, TLV.quatSmoothMode,
  TLV.audioEnabled, TLV.potControlsThreshold, TLV.thresholdFixed, TLV.debounceMs,
  TLV.audioBpfEnabled,
  // Three of the navigation fields (joyMaxSpeedDegS, joyFullAngleDeg, joyCurveX10) only show in joystick mode.
  TLV.navMode, TLV.joyMaxSpeedDegS, TLV.joyFullAngleDeg, TLV.joyCurveX10,
  TLV.co2Enabled, TLV.co2TriggerPpm, TLV.virtualPeakPpm, TLV.virtualDelayMs, TLV.virtualHoldMs,
  TLV.bleName, TLV.pin,
]);

type Tier = 'basico' | 'avancado';
const tierOf = (f: Field): Tier => (BASIC_IDS.has(f.id) ? 'basico' : 'avancado');

/**
 * Firmware defaults, mirroring `firmware/Gaiaball/src/logic/GaiaConfig.h`. The form is
 * pre-filled with these so no field ever starts blank: an empty number box says nothing
 * about what the device will do, while the default says exactly that. The PIN is absent
 * on purpose — it is write-only, and showing one would suggest it had been read.
 */
export const FIELD_DEFAULTS: ReadonlyMap<number, number | string> = new Map<number, number | string>([
  [TLV.orientationMode, 0],
  [TLV.imuChip, 0],
  [TLV.imuNotifyHz, 50],
  [TLV.gyroFsSel, 2],
  [TLV.imuDlpfMode, 0],
  [TLV.magAxisRemap, 8],
  [TLV.fusionGainX100, 50],
  [TLV.accelRejectionDeg, 10],
  [TLV.magRejectionDeg, 10],
  [TLV.rejectionTimeoutS, 5],
  [TLV.biasRestThDpsX100, 200],
  [TLV.biasMaxDpsX100, 1000],
  [TLV.biasTauS, 4],
  [TLV.quatSmoothMode, 0],
  [TLV.slerpAmount, 0],
  [TLV.oneEuroFcMinX100, 100],
  [TLV.oneEuroBetaX1000, 20],
  [TLV.accelLpAlphaX1000, 0],
  [TLV.accelHampelKX10, 0],
  [TLV.audioEnabled, 1],
  [TLV.audioFastAlphaX1000, 250],
  [TLV.audioSlowAlphaX1000, 995],
  [TLV.debounceMs, 300],
  [TLV.potControlsThreshold, 1],
  [TLV.thresholdFixed, 100000],
  [TLV.potEmaAlphaX1000, 300],
  [TLV.audioBpfEnabled, 1],
  [TLV.audioHpHz, 10],
  [TLV.audioHpQX100, 500],
  [TLV.audioLpHz, 200],
  [TLV.audioLpQX100, 500],
  [TLV.audioRawStream, 0],
  [TLV.co2Enabled, 1],
  [TLV.co2StaleTimeoutMs, 5000],
  [TLV.virtualPeakPpm, 2100],
  [TLV.virtualDelayMs, 1000],
  [TLV.virtualHoldMs, 17000],
  [TLV.co2RawLogMode, 0],
  [TLV.co2EmaAlphaX1000, 1000],
  [TLV.co2TriggerPpm, 1200],
  [TLV.co2TriggerHystPpm, 50],
  [TLV.navMode, 1],
  [TLV.navNotifyHz, 50],
  [TLV.navRef, '-22.905556,-47.060833'],
  [TLV.joyMaxSpeedDegS, 90],
  [TLV.joyFullAngleDeg, 60],
  [TLV.joyCurveX10, 30],
  [TLV.navStartDegS, 20],
  [TLV.navStopDegS, 5],
  [TLV.navStopMs, 420],
  [TLV.joyStartDegSX10, 10],
  [TLV.joyStopDegSX10, 5],
  [TLV.serialLogEnabled, 0],
  [TLV.bleName, 'Globo_1'],
]);

/** Exported for the test that keeps the two tables honest. */
export const CONFIG_FIELDS: readonly Field[] = FIELDS;
export const BASIC_FIELD_IDS: ReadonlySet<number> = BASIC_IDS;

function decodeValue(f: Field, dv: DataView): number | string | null {
  if (f.kind === 'text') return new TextDecoder().decode(dv);
  if (f.kind === 'navref') {
    const r = decodeNavRef(dv);
    return r ? navRefToString(r) : null;
  }
  switch (dv.byteLength) {
    case 1: return dv.getUint8(0);
    case 2: return dv.getUint16(0, true);
    case 4: return dv.getUint32(0, true);
    default: return null;
  }
}

/** Register values as the form holds them -> what goes on the wire (navRef: "lat,lon" -> 8 bytes). */
export function toWireRegs(regs: TlvWrite[]): TlvWrite[] {
  return regs.map((r) => {
    if (r.id !== TLV.navRef || typeof r.value !== 'string') return r;
    const ref = parseNavRefString(r.value);
    if (!ref) throw new RangeError('navRef must be "lat,lon"');
    return { id: r.id, value: encodeNavRef(ref) };
  });
}

/** Validates the canonical navRef string ("lat,lon"): pt-BR message or null. */
export function navRefError(label: string, v: string): string | null {
  const ref = parseNavRefString(v);
  if (!ref) return `${label}: informe latitude e longitude numéricas.`;
  if (Math.abs(ref.lat) > 90) return `${label}: a latitude vai de −90 a 90.`;
  if (Math.abs(ref.lon) > 180) return `${label}: a longitude vai de −180 a 180.`;
  return null;
}

const STOP_RULE_DEFAULTS: ReadonlyMap<number, number> = new Map([
  [TLV.navStartDegS, 20], [TLV.navStopDegS, 5], [TLV.joyStartDegSX10, 10], [TLV.joyStopDegSX10, 5],
]);
/**
 * Mirror of the firmware cross rules on the RESULTING state (written value, else last known, else
 * default): stop < start for both modes. The firmware rejects the whole write otherwise.
 */
export function stopRuleError(regs: TlvWrite[], snapshot: ReadonlyMap<number, number | string>): string | null {
  const ids = [...STOP_RULE_DEFAULTS.keys()];
  if (!regs.some((r) => ids.includes(r.id))) return null;
  const finalV = (id: number): number =>
    Number(regs.find((r) => r.id === id)?.value ?? snapshot.get(id) ?? STOP_RULE_DEFAULTS.get(id));
  const mStart = finalV(TLV.navStartDegS), mStop = finalV(TLV.navStopDegS);
  if (mStop >= mStart)
    return `Espelho: a velocidade de parada (${mStop} °/s) precisa ser menor que a de partida (${mStart} °/s).`;
  const jStart = finalV(TLV.joyStartDegSX10), jStop = finalV(TLV.joyStopDegSX10);
  if (jStop >= jStart)
    return `Joystick: a velocidade de parada (${(jStop / 10).toFixed(1).replace('.', ',')} °/s) precisa ser menor que a de partida (${(jStart / 10).toFixed(1).replace('.', ',')} °/s).`;
  return null;
}

export function mountConfigPanel(root: HTMLElement, client: GaiaballClient, store: PresetStore = new PresetStore()): void {
  root.innerHTML = `
    <h2>Configuração</h2>
    <div class="row">
      <button type="button" data-act="read">Ler do dispositivo</button>
      <button type="button" data-act="apply" class="primary">Aplicar</button>
    </div>
    <p class="msg" data-el="hint">Os campos mostram os padrões de fábrica. Leia do dispositivo para ver o que está gravado — aplicar envia só o que mudou, confere a aceitação e grava de forma permanente.</p>
    <p class="msg" data-el="msg" role="status"></p>
    <div class="tabs" role="tablist" aria-label="Parâmetros">
      <button type="button" class="tab" role="tab" id="tab-basico" aria-controls="pane-basico" aria-selected="true">Básico</button>
      <button type="button" class="tab" role="tab" id="tab-avancado" aria-controls="pane-avancado" aria-selected="false" tabindex="-1"
        data-info="Ajustes da cadeia de sinais: faixa e filtro da IMU, ganho e rejeições da fusão, bias do giroscópio, parâmetros da suavização e dos filtros de áudio e CO₂. O número diz quantos parâmetros vivem aqui; o ponto acende quando algum está fora do padrão de fábrica.">Avançado
        <span class="tab-count" data-el="advCount"></span></button>
    </div>
    <form data-el="form" onsubmit="return false">
      <div role="tabpanel" id="pane-basico" aria-labelledby="tab-basico" data-el="pane-basico"></div>
      <div role="tabpanel" id="pane-avancado" aria-labelledby="tab-avancado" data-el="pane-avancado" hidden></div>
    </form>
    <h3>Presets</h3>
    <div class="row"><input data-el="pname" type="text" placeholder="Nome do preset" aria-label="Nome do preset" />
      <button type="button" data-act="psave">Salvar preset</button></div>
    <div class="row"><select data-el="plist" aria-label="Presets salvos"></select>
      <button type="button" data-act="pload">Carregar</button>
      <button type="button" data-act="pdel">Excluir</button></div>
    <div class="row"><button type="button" data-act="pexport">Exportar JSON</button>
      <button type="button" data-act="pimport">Importar JSON</button>
      <input data-el="pfile" type="file" accept="application/json,.json" hidden /></div>
    <p class="msg" data-el="pmsg" role="status"></p>`;

  const q = <T extends HTMLElement>(sel: string): T => root.querySelector<T>(sel)!;
  const msg = q('[data-el=msg]');
  const hint = q('[data-el=hint]');
  const advCount = q('[data-el=advCount]');
  const tabs: Record<Tier, HTMLButtonElement> = {
    basico: q<HTMLButtonElement>('#tab-basico'),
    avancado: q<HTMLButtonElement>('#tab-avancado'),
  };
  const pmsg = q('[data-el=pmsg]');
  const inputs = new Map<number, HTMLInputElement | HTMLSelectElement>();
  /** On/off checkboxes for fields with offValue (one toggle per filter). */
  const toggles = new Map<number, HTMLInputElement>();
  /** The row element of every field, so `visible` can hide it. */
  const rows = new Map<number, HTMLElement>();
  /** Second input (longitude) of the navRef field; the first one lives in `inputs`. */
  let navLon!: HTMLInputElement;

  const panes: Record<Tier, HTMLElement> = {
    basico: q('[data-el=pane-basico]'),
    avancado: q('[data-el=pane-avancado]'),
  };

  // A group with fields in both tiers appears in both tabs, carrying only the fields of
  // that tier: "Áudio" keeps its operating controls up front and its envelopes behind.
  for (const g of GROUPS) {
    for (const tier of ['basico', 'avancado'] as Tier[]) {
      const fields = g.fields.filter((f) => tierOf(f) === tier);
      if (fields.length === 0) continue;
      buildGroup(g.title, fields, panes[tier]);
    }
  }

  function buildGroup(title: string, fields: readonly Field[], pane: HTMLElement): void {
    const fs = document.createElement('fieldset');
    fs.innerHTML = `<legend>${title}</legend>`;
    for (const f of fields) {
      // navRef has two inputs, so its row is a div (a <label> would bind only the first one).
      const label = document.createElement(f.kind === 'navref' ? 'div' : 'label');
      label.className = 'cfg-field';
      rows.set(f.id, label);
      const name = document.createElement('span');
      name.className = 'cfg-name';
      name.append(f.label, infoDot(f.info));
      let el: HTMLInputElement | HTMLSelectElement;
      let extra: HTMLElement | null = null;
      if (f.kind === 'navref') {
        const mk = (aria: string, lim: number): HTMLInputElement => {
          const i = document.createElement('input');
          i.type = 'number';
          i.step = '0.000001';
          i.min = String(-lim);
          i.max = String(lim);
          i.setAttribute('aria-label', `${f.label}: ${aria}`);
          i.placeholder = aria;
          return i;
        };
        const lat = mk('latitude', 90);
        navLon = mk('longitude', 180);
        el = lat;
        const wrap = document.createElement('span');
        wrap.className = 'cfg-navref';
        wrap.append(lat, navLon);
        extra = wrap;
      } else if (f.kind === 'select') {
        el = document.createElement('select');
        for (const [v, t] of resolveOptions(f, new Map())) el.add(new Option(t, String(v)));
      } else {
        el = document.createElement('input');
        el.type = f.kind === 'checkbox' ? 'checkbox' : f.kind === 'pin' ? 'password' : f.kind === 'text' ? 'text' : f.kind;
        if (f.min !== undefined) el.min = String(f.min / (f.scale ?? 1));
        if (f.max !== undefined) el.max = String(f.max / (f.scale ?? 1));
        if (f.scale) el.step = String(1 / f.scale);
        if (f.kind === 'text') el.maxLength = 16;
        if (f.kind === 'pin') { el.maxLength = 4; el.inputMode = 'numeric'; el.autocomplete = 'off'; el.placeholder = '····'; }
      }
      el.dataset.reg = String(f.id);
      if (f.offValue !== undefined) {
        const tg = document.createElement('input');
        tg.type = 'checkbox';
        tg.className = 'filter-toggle';
        tg.title = 'Ligar/desligar este filtro';
        tg.setAttribute('aria-label', `Ligar/desligar: ${f.label}`);
        tg.checked = true;
        name.prepend(tg);
        toggles.set(f.id, tg);
        tg.addEventListener('change', () => label.classList.toggle('filter-off', !tg.checked));
      }
      label.append(name, extra ?? el);
      fs.append(label);
      inputs.set(f.id, el);
      if (f.id === TLV.imuChip) {
        // Visible (not tooltip-only) because a switch silently does nothing until the ball reboots.
        // Sits outside the <label> so clicking the button does not focus the select.
        const b = document.createElement('div');
        b.className = 'banner cfg-reboot';
        b.dataset.el = 'imu-reboot';
        b.hidden = true;
        b.setAttribute('role', 'status');
        b.innerHTML = '<span>A troca de IMU só vale depois de reiniciar a bola.</span> <button type="button" data-act="imu-reboot">Reiniciar agora</button>';
        fs.append(b);
      }
    }
    pane.append(fs);
  }

  /** Last values known to be on the device (read or read-back). PIN never appears here. */
  let snapshot = new Map<number, number | string>();
  /** Fields the user (or a loaded preset) touched that the device never reported. */
  const dirty = new Set<number>();
  for (const [id, el] of inputs) {
    el.addEventListener('input', () => { dirty.add(id); refreshAdvCount(); });
    el.addEventListener('change', () => {
      refreshAdvCount();
      if (id === TLV.imuChip) { refreshDependentOptions(); refreshImuBanner(); }
      if (id === TLV.navMode) refreshVisibility();
    });
  }
  navLon.addEventListener('input', () => { dirty.add(TLV.navRef); refreshAdvCount(); });
  navLon.addEventListener('change', () => refreshAdvCount());
  for (const [id, tg] of toggles) tg.addEventListener('change', () => { dirty.add(id); refreshAdvCount(); });

  const getValue = (f: Field): number | string | null => {
    const el = inputs.get(f.id)!;
    // A filter whose toggle is off writes its "off" value, whatever the input shows.
    if (f.offValue !== undefined && !toggles.get(f.id)!.checked) return f.offValue;
    if (f.kind === 'checkbox') return (el as HTMLInputElement).checked ? 1 : 0;
    if (f.kind === 'text' || f.kind === 'pin') return el.value;
    if (f.kind === 'navref') {
      // One canonical string "lat,lon" (6 decimals) keeps snapshot/compare/dirty working with ===.
      if (el.value.trim() === '' || navLon.value.trim() === '') return null;
      const lat = Number(el.value), lon = Number(navLon.value);
      return Number.isFinite(lat) && Number.isFinite(lon) ? navRefToString({ lat, lon }) : null;
    }
    if (el.value.trim() === '') return null;
    if (f.scale) return Math.round(Number(el.value) * f.scale);
    return Number(el.value);
  };
  const setValue = (f: Field, v: number | string): void => {
    const el = inputs.get(f.id)!;
    if (f.offValue !== undefined) {
      const on = Number(v) !== f.offValue;
      toggles.get(f.id)!.checked = on;
      el.closest('.cfg-field')?.classList.toggle('filter-off', !on);
      // Keep a sensible value visible while off, so turning the filter back on works.
      el.value = String(on ? v : (f.onDefault ?? f.min ?? 0));
      return;
    }
    if (f.kind === 'checkbox') (el as HTMLInputElement).checked = Number(v) !== 0;
    else if (f.kind === 'navref') {
      const ref = parseNavRefString(String(v));
      el.value = ref ? ref.lat.toFixed(6) : '';
      navLon.value = ref ? ref.lon.toFixed(6) : '';
    }
    else if (f.scale) el.value = (Number(v) / f.scale).toFixed(Math.round(Math.log10(f.scale)));
    else el.value = String(v);
  };
  /** Shows or hides rows whose `visible` depends on the form (joystick sensitivity follows navMode). */
  const refreshVisibility = (): void => {
    const view = formView();
    for (const f of FIELDS) if (f.visible) rows.get(f.id)!.hidden = !f.visible(view);
  };
  /** Live form values by register id, for fields whose options depend on other fields. */
  const formView = (): Map<number, unknown> => {
    const m = new Map<number, unknown>();
    for (const f of FIELDS) m.set(f.id, getValue(f));
    return m;
  };
  /** Relabel selects whose options are a function of the form (DLPF follows imuChip), keeping the value. */
  const refreshDependentOptions = (): void => {
    const view = formView();
    for (const f of FIELDS) {
      if (typeof f.options !== 'function') continue;
      const el = inputs.get(f.id) as HTMLSelectElement;
      const keep = el.value;
      el.replaceChildren(...f.options(view).map(([v, t]) => new Option(t, String(v))));
      el.value = keep;
    }
  };
  /** TLV 0x35 from the last read; has no Field because populate only keeps registers that do. */
  let imuDetected: number | undefined;
  const imuBanner = q('[data-el=imu-reboot]');
  const refreshImuBanner = (): void => {
    const sel = getValue(byId(TLV.imuChip));
    const read = snapshot.get(TLV.imuChip);
    imuBanner.hidden = !(typeof sel === 'number' &&
      imuRebootPending(sel, typeof read === 'number' ? read : undefined, imuDetected));
  };
  q('[data-act=imu-reboot]').addEventListener('click', () => {
    if (!confirm('Reiniciar o dispositivo? A conexão será perdida.')) return;
    void client.sendCommand(0x13).then(() => say('Reiniciar: enviado.'), fail);
  });
  const populate = (regs: Map<number, DataView>): void => {
    snapshot = new Map();
    const det = regs.get(TLV.imuDetected);
    imuDetected = det && det.byteLength === 1 ? det.getUint8(0) : undefined;
    dirty.clear();
    for (const f of FIELDS) {
      if (f.id === TLV.pin) continue; // firmware never sends the PIN
      const dv = regs.get(f.id);
      if (!dv) continue;
      const v = decodeValue(f, dv);
      if (v === null) continue;
      snapshot.set(f.id, v);
      setValue(f, v);
    }
    (inputs.get(TLV.pin) as HTMLInputElement).value = '';
    refreshDependentOptions();
    refreshVisibility();
    refreshImuBanner();
    hint.textContent = 'Lido do dispositivo. Aplicar envia só o que mudou, confere a aceitação e grava de forma permanente.';
    refreshAdvCount();
  };

  /** Fills every field with the firmware default. Does NOT mark anything dirty, so a
   *  pre-filled value is never written to the device unless the person edits it. */
  const seedDefaults = (): void => {
    for (const f of FIELDS) {
      const d = FIELD_DEFAULTS.get(f.id);
      if (d !== undefined) setValue(f, d);
    }
    refreshDependentOptions();
    refreshVisibility();
  };

  /** How many advanced parameters currently sit away from the firmware default. */
  const offDefaultCount = (): number => {
    let n = 0;
    for (const f of FIELDS) {
      if (tierOf(f) !== 'avancado') continue;
      const d = FIELD_DEFAULTS.get(f.id);
      if (d === undefined) continue;
      if (getValue(f) !== d) n++;
    }
    return n;
  };

  const advTotal = FIELDS.filter((f) => tierOf(f) === 'avancado').length;
  const refreshAdvCount = (): void => {
    const off = offDefaultCount();
    advCount.textContent = String(advTotal);
    tabs.avancado.classList.toggle('has-off', off > 0);
    tabs.avancado.title = off > 0
      ? `${advTotal} parâmetros · ${off} fora do padrão de fábrica`
      : `${advTotal} parâmetros, todos no padrão de fábrica`;
  };

  const selectTab = (tier: Tier): void => {
    for (const t of ['basico', 'avancado'] as Tier[]) {
      const on = t === tier;
      tabs[t].setAttribute('aria-selected', String(on));
      tabs[t].tabIndex = on ? 0 : -1;
      panes[t].hidden = !on;
    }
  };
  for (const t of ['basico', 'avancado'] as Tier[]) {
    tabs[t].addEventListener('click', () => selectTab(t));
  }
  // Arrow keys move between tabs, as a tablist is expected to.
  for (const t of ['basico', 'avancado'] as Tier[]) {
    tabs[t].addEventListener('keydown', (ev) => {
      const k = (ev as KeyboardEvent).key;
      if (k !== 'ArrowLeft' && k !== 'ArrowRight') return;
      ev.preventDefault();
      const next: Tier = t === 'basico' ? 'avancado' : 'basico';
      selectTab(next);
      tabs[next].focus();
    });
  }

  /** Validate one field value; returns a pt-BR error or null. */
  const validate = (f: Field, v: number | string | null): string | null => {
    if (v === null || (typeof v === 'number' && !Number.isFinite(v))) return `${f.label}: informe um número.`;
    if (f.kind === 'navref') return navRefError(f.label, String(v));
    if (typeof v === 'number') {
      const sc = f.scale ?? 1;
      const shown = (n: number): string => (sc === 1 ? String(n) : (n / sc).toFixed(1).replace('.', ','));
      if (!Number.isInteger(v)) return `${f.label}: use um número inteiro.`;
      if (f.min !== undefined && v < f.min) return `${f.label}: mínimo ${shown(f.min)}.`;
      if (f.max !== undefined && v > f.max) return `${f.label}: máximo ${shown(f.max)}.`;
    }
    if (f.id === TLV.pin && !/^\d{4}$/.test(String(v))) return `${f.label}: exatamente 4 dígitos.`;
    try { encodeTlv(toWireRegs([{ id: f.id, value: v }])); }
    catch (e) {
      if (e instanceof RangeError) return `${f.label}: valor inválido (${f.kind === 'text' ? 'de 1 a 16 bytes' : 'fora da faixa'}).`;
      throw e;
    }
    return null;
  };

  const say = (t: string): void => { msg.textContent = t; };
  const fail = (e: unknown): void => say(e instanceof Error ? e.message : String(e));
  const hasPin = (f: Field): boolean => f.id === TLV.pin;

  /** Mirror of the firmware rule: the band-pass must be a real band (LP > HP). */
  const BAND_DEFAULTS: ReadonlyMap<number, number> = new Map([[TLV.audioHpHz, 10], [TLV.audioLpHz, 200]]);
  const bandError = (regs: TlvWrite[]): string | null => {
    const ids = [...BAND_DEFAULTS.keys()];
    if (!regs.some((r) => ids.includes(r.id))) return null;
    const finalV = (id: number): number =>
      Number(regs.find((r) => r.id === id)?.value ?? snapshot.get(id) ?? BAND_DEFAULTS.get(id));
    if (finalV(TLV.audioLpHz) <= finalV(TLV.audioHpHz))
      return 'Banda inválida: o low-pass precisa ficar acima do high-pass (LP > HP).';
    return null;
  };

  /** Collect only fields that differ from the snapshot (PIN: only when typed). */
  const collectChanged = (): { regs: TlvWrite[]; error: string | null } => {
    const regs: TlvWrite[] = [];
    for (const f of FIELDS) {
      const v = getValue(f);
      if (hasPin(f)) {
        if (v === '') continue;
      } else if (snapshot.has(f.id)) {
        if (snapshot.get(f.id) === v) continue;
      } else if (!dirty.has(f.id)) continue; // never reported by the device and untouched
      const err = validate(f, v);
      if (err) return { regs: [], error: err };
      regs.push({ id: f.id, value: v as number | string });
    }
    const bandErr = bandError(regs);
    if (bandErr) return { regs: [], error: bandErr };
    const stopErr = stopRuleError(regs, snapshot);
    if (stopErr) return { regs: [], error: stopErr };
    return { regs, error: null };
  };

  /** Single path for every configuration read (button, Aplicar, connect): the panels listen to 'config'. */
  const publish = (regs: Map<number, DataView>): void => {
    client.dispatchEvent(new CustomEvent('config', { detail: regs }));
  };
  const doRead = async (): Promise<void> => {
    publish(await client.readConfig());
  };

  q('[data-act=read]').addEventListener('click', () => {
    void doRead().then(() => say('Configuração lida do dispositivo.'), fail);
  });

  q('[data-act=apply]').addEventListener('click', () => {
    void (async () => {
      if (snapshot.size === 0) { say('Leia a configuração do dispositivo antes de aplicar.'); return; }
      const { regs, error } = collectChanged();
      if (error) { say(error); return; }
      if (regs.length === 0) { say('Nenhuma alteração para aplicar.'); return; }
      await client.writeConfig(toWireRegs(regs));
      // The firmware cannot signal rejected writes: read back and compare.
      const back = await client.readConfig();
      const rejected: string[] = [];
      for (const r of regs) {
        if (r.id === TLV.pin) continue;
        const f = byId(r.id);
        const dv = back.get(r.id);
        const got = dv ? decodeValue(f, dv) : null;
        if (got !== r.value) rejected.push(f.label);
      }
      publish(back);
      const pinNote = regs.some((r) => r.id === TLV.pin) ? ' Novo PIN enviado (não verificável).' : '';
      if (rejected.length > 0) {
        say(`⚠ alguns valores não foram aceitos (desbloqueou com o PIN?): ${rejected.join(', ')}.${pinNote}`);
        return;
      }
      // Changes made through this UI are meant to survive power cycles: persist now.
      await client.sendCommand(0x11);
      say(`Aplicado, verificado e salvo no dispositivo (${regs.length} registro(s)).${pinNote}`);
    })().catch(fail);
  });

  // ---- presets ----
  const plist = q<HTMLSelectElement>('[data-el=plist]');
  const pname = q<HTMLInputElement>('[data-el=pname]');
  const pfile = q<HTMLInputElement>('[data-el=pfile]');
  const refreshPresets = (select?: string): void => {
    plist.replaceChildren(...store.list().map((p) => new Option(p.name, p.name)));
    if (select) plist.value = select;
  };
  refreshPresets();

  // Start on the basic tab with every field showing the firmware default, so the panel
  // is readable before anything has been read from the device.
  seedDefaults();
  selectTab('basico');
  refreshAdvCount();

  q('[data-act=psave]').addEventListener('click', () => {
    const name = pname.value.trim();
    if (!name) { pmsg.textContent = 'Informe um nome para o preset.'; return; }
    if (snapshot.size === 0) { pmsg.textContent = 'Leia a configuração do dispositivo antes de salvar um preset.'; return; }
    const regs: TlvWrite[] = [];
    for (const f of FIELDS) {
      if (isPerBall(f)) continue; // PIN, magnetometer calibration, IMU chip and axis remap belong to one ball
      const v = getValue(f);
      if (v === null) continue; // empty numeric field (never read): not part of the preset
      const err = validate(f, v);
      if (err) { pmsg.textContent = err; return; }
      regs.push({ id: f.id, value: v as number | string });
    }
    store.save(name, regs);
    refreshPresets(name);
    pmsg.textContent = `Preset "${name}" salvo.`;
  });
  q('[data-act=pload]').addEventListener('click', () => {
    const p = store.list().find((x) => x.name === plist.value);
    if (!p) { pmsg.textContent = 'Selecione um preset.'; return; }
    for (const r of p.regs) {
      const f = FIELDS.find((x) => x.id === r.id);
      // Per-ball registers (PIN, magnetometer calibration, IMU chip, axis remap) are never
      // part of a preset, even one saved by an older version that did include them.
      if (f && !isPerBall(f) && typeof r.value !== 'object') { setValue(f, r.value); dirty.add(f.id); }
    }
    refreshVisibility();
    pmsg.textContent = `Preset "${p.name}" carregado no formulário. Clique em Aplicar para enviar.`;
  });
  q('[data-act=pdel]').addEventListener('click', () => {
    const name = plist.value;
    if (!name || !confirm(`Excluir o preset "${name}"?`)) return;
    store.remove(name);
    refreshPresets();
    pmsg.textContent = `Preset "${name}" excluído.`;
  });
  q('[data-act=pexport]').addEventListener('click', () => {
    const url = URL.createObjectURL(new Blob([store.exportJson()], { type: 'application/json' }));
    const a = document.createElement('a');
    a.href = url;
    a.download = 'gaiaball-presets.json';
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  });
  q('[data-act=pimport]').addEventListener('click', () => pfile.click());
  pfile.addEventListener('change', () => {
    const file = pfile.files?.[0];
    if (!file) return;
    void file.text().then((t) => {
      const n = store.importJson(t);
      refreshPresets();
      pmsg.textContent = n > 0 ? `${n} preset(s) importado(s).` : 'Arquivo inválido: nada foi importado.';
    }, () => { pmsg.textContent = 'Não foi possível ler o arquivo.'; }).finally(() => { pfile.value = ''; });
  });

  // ---- connection gating (presets stay usable offline) ----
  const gated = [...inputs.values(), navLon, ...toggles.values(), q<HTMLButtonElement>('[data-act=read]'), q<HTMLButtonElement>('[data-act=apply]')];
  const render = (d: StatusDetail): void => {
    for (const el of gated) el.disabled = !d.connected;
    if (!d.connected) { snapshot = new Map(); imuDetected = undefined; refreshImuBanner(); }
  };
  client.addEventListener('status', (ev) => render((ev as CustomEvent<StatusDetail>).detail));
  client.addEventListener('config', (ev) => populate((ev as CustomEvent<Map<number, DataView>>).detail));
  render({ connected: client.connected, name: null });
}
