# Testes do firmware Micromouse (host)

Suíte de testes **unitários, de integração e de sistema** que roda no PC
(Windows), sem precisar do ESP32, do QEMU nem do ambiente ESP-IDF. Os arquivos
`.cpp` reais do firmware são compilados com o MSVC (`cl.exe`) contra *mocks* das
APIs do ESP-IDF.

## Como rodar

```powershell
# a partir desta pasta (test/host)
.\run_tests.ps1

# apenas um alvo
.\run_tests.ps1 maze
```

O script localiza o `vcvars64.bat` (Visual Studio Build Tools), importa o
ambiente do compilador, compila cada alvo em `build/` e executa. Sai com código
0 se tudo passar, 1 se algo falhar.

> Requisito: Visual Studio Build Tools 2022 (ou Community) com o componente
> "Desenvolvimento para desktop com C++". O `cJSON.c` usado pelos testes vem de
> `managed_components/espressif__cjson/`.

## O que é coberto

| Alvo | Módulo testado | Tipo | Resumo |
|------|----------------|------|--------|
| `test_maze` | `main/maze` | unitário + **integração** | Geometria, registro de paredes (simetria), flood fill (Manhattan, respeito a paredes, células inalcançáveis), gradiente, rota ótima e o **ciclo completo da máquina de estados** (Explorar → Refinar → Retornar → FastRun) em labirintos abertos, com paredes e com objetivo bloqueado. |
| `test_envio_dados` | `main/envio_dados` | unitário | Cada pacote JSON (tipos 0–5) é serializado e re-parseado com cJSON para conferir os campos; propagação de erro e rejeição de URL nula. |
| `test_battery` | `main/battery` | unitário | `voltageToSOC` (extremos e saturação), contagem de Coulomb, correção por tensão em repouso, saturação 0–100 % e o filtro de média móvel + LPF. |
| `test_motor` | `main/motor` | unitário | Lógica da ponte H (sentido para frente/ré/freio), saturação do duty em 1023, liberação do standby e leitura do encoder (PCNT). |
| `test_telemetria` | `main/telemetria` | unitário + integração | *Gating* por Wi-Fi, timestamp relativo ao início, e o *heartbeat* de inatividade (limiar, reset por atividade, supressão sem Wi-Fi). |
| `test_system` | `battery` + `maze` + `telemetria` + `envio_dados` | **sistema (E2E)** | Cadeia completa com os módulos **reais** ligados entre si (só Wi-Fi/HTTP/relógio/INA226 mockados): uma missão do robô gera o fluxo de telemetria que um *backend simulado* faz o *parse* a partir do JSON que cruzou a rede. Cobre missão completa (config→movimentos→rota→fim, timestamps monotônicos, descarga da bateria), fidelidade das paredes reportadas (sem fantasmas), gating por queda/reconexão de Wi-Fi, propagação e recuperação de falha de rede, *heartbeat* carregando o SOC atual, e alerta de temperatura no meio da corrida. |

Total atual: **54 testes / 230 verificações**.

## Pirâmide de testes

- **Unitário** — um módulo isolado contra mocks (`test_battery`, `test_motor`,
  `test_envio_dados`, partes de `test_maze`/`test_telemetria`).
- **Integração** — a máquina de estados do labirinto de ponta a ponta, ou a
  telemetria sobre um `envio_dados` mockado (`test_maze`, `test_telemetria`).
- **Sistema (E2E)** — os módulos **reais** do firmware compilados juntos, com
  apenas as bordas de hardware/rede mockadas, validando o comportamento
  observável no JSON que sai pela rede (`test_system`).

## Estrutura

```
test/host/
  framework.hpp          Framework minimalista (TEST_CASE / CHECK* / REQUIRE)
  run_tests.ps1          Compila e roda todos os alvos com o MSVC
  test_*.cpp             Um arquivo de teste por módulo
  mocks/                 Mocks das APIs do ESP-IDF / drivers / bibliotecas
    esp_err.h  esp_log.h  esp_timer.h(+.cpp)
    esp_http_client.h(+_mock.cpp)
    driver/gpio.h ledc.h pulse_cnt.h i2c_types.h  (+ driver_mock.cpp)
    ina226.hpp(+_mock.cpp)   i2c_manager_mock.cpp
    wifi.hpp(+_mock.cpp)     mock_envio_dados.h(+.cpp)
  build/                 Artefatos de compilação (ignorado pelo git)
```

Os mocks expõem ajudantes (`mock_*`) para os testes injetarem leituras de
sensores, avançarem o relógio virtual, controlarem o estado do Wi-Fi e
inspecionarem o que foi "enviado".

## Escrevendo um novo teste

```cpp
#include "framework.hpp"

TEST_CASE(meu_caso) {
    CHECK(1 + 1 == 2);
    CHECK_EQ(soma(2, 3), 5);
    CHECK_FLOAT_EQ(media, 7.5, 1e-3);
    REQUIRE(ptr != nullptr);   // aborta o caso se falhar
}

TEST_MAIN()  // uma vez por executável
```

Para um módulo novo, adicione um bloco em `$targets` no `run_tests.ps1` listando
os `.cpp` e os diretórios de include.

## Módulos não cobertos aqui (e por quê)

`imu`, `vl53l0x`, `wifi`, `sd_card` e `i2c_manager` são essencialmente *cola* de
hardware: quase toda a lógica é chamada direta de drivers I2C/SPI/GPIO ou de
bibliotecas externas (MPU9250, VL53L0X, esp_wifi). Não há algoritmo isolável que
um teste de host validaria de forma significativa — eles exigem **teste no alvo
(on-target / HIL)** com o sensor real ou em QEMU. Para isso, o caminho indicado é
o framework Unity do próprio ESP-IDF (`idf.py create-component`/`pytest-embedded`)
executado no ESP32.

## Notas

- Os mocks reproduzem só a superfície de API que cada módulo usa; ao mexer no
  firmware, pode ser preciso ampliar um mock.
- O relógio dos testes é virtual (`mock_timer_*`), então os testes de tempo são
  determinísticos e instantâneos.
