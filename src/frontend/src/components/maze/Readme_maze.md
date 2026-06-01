# Maze Viewer - Mock por JSON via variáveis de ambiente

Este diretório contém a visualização do labirinto e a fonte de dados de teste usada quando não há telemetria real do ESP32.

## Objetivo

O `MazeViewer` pode funcionar de duas formas:

1. com telemetria real, recebendo pacotes do WebSocket do backend;
2. com telemetria mock, usando pacotes JSON definidos localmente.

A troca entre esses modos é feita por variáveis de ambiente no frontend.

## Variáveis de ambiente

Crie ou edite o arquivo `.env` do frontend e defina:

```env
VITE_USE_MAZE_MOCK=true
VITE_MAZE_MOCK_SIZE=8
```

### Significado

- `VITE_USE_MAZE_MOCK`
  - `true`: usa os dados JSON locais do mock.
  - qualquer outro valor, ou ausente: usa o WebSocket real.

- `VITE_MAZE_MOCK_SIZE`
  - escolhe qual conjunto JSON será carregado no mock.
  - valores aceitos: `4`, `8` ou `16`.

## Exemplos

### Usar mock 4x4

```env
VITE_USE_MAZE_MOCK=true
VITE_MAZE_MOCK_SIZE=4
```

### Usar mock 8x8

```env
VITE_USE_MAZE_MOCK=true
VITE_MAZE_MOCK_SIZE=8
```

### Usar mock 16x16

```env
VITE_USE_MAZE_MOCK=true
VITE_MAZE_MOCK_SIZE=16
```

### Voltar para a telemetria real

```env
VITE_USE_MAZE_MOCK=false
```

ou simplesmente remova as variáveis do arquivo `.env`.

## Onde os JSONs ficam

Os pacotes mockados ficam em [mockTelemetry.ts](mockTelemetry.ts).

O objeto principal é `MOCK_TELEMETRY_BY_SIZE`, que contém os cenários para:

- `4`
- `8`
- `16`

Cada cenário é composto por uma sequência de pacotes JSON com esta ideia geral:

1. pacote inicial;
2. pacote de rota;
3. pacotes de movimentação;
4. pacote final.

## Como trocar os dados JSON

Se quiser alterar o comportamento do labirinto, edite o arquivo [mockTelemetry.ts](mockTelemetry.ts) e modifique os JSONs dentro de `MOCK_TELEMETRY_BY_SIZE`.

Exemplo de pacote inicial:

```json
{
  "kind": "inicial",
  "payload": {
    "id_corrida": 8,
    "timestamp_ms": 0,
    "dimensao": 8,
    "tentativa": 1,
    "bateria": 100
  }
}
```

Exemplo de pacote de movimentação:

```json
{
  "kind": "movimentacao",
  "payload": {
    "id_corrida": 8,
    "timestamp_ms": 1500,
    "x": 2,
    "y": 1,
    "w": 5
  }
}
```

## O que o MazeViewer faz com esses dados

- `dimensao` define o tamanho do labirinto.
- `x` e `y` definem a posição do robô.
- `w` é usado como bitmask das paredes detectadas.
- `sucesso` e `v_med` fecham a sessão.

## Observação importante

O mock não lê o arquivo `.env` diretamente. Quem lê a variável é o frontend Vite durante a compilação/execução.

Depois de alterar o `.env`, reinicie o frontend para aplicar as mudanças.
