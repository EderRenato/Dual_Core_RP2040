# ☁️ Estação Meteorológica Multicore com RP2040
Este projeto é uma estação meteorológica desenvolvida para o microcontrolador Raspberry Pi Pico (RP2040), utilizando seus dois núcleos (multicore) para dividir as tarefas de aquisição de dados e gerenciamento da interface do usuário.

O principal objetivo deste projeto é aplicar e reforçar os conhecimentos sobre programação multicore, comunicação inter-core (FIFO) e gerenciamento de periféricos I2C, adquiridos durante a Residência Tecnológica Embarcatech.

## 👥 Autores
<ul>
    <li> <a href="https://github.com/Brunis1108">Bruna Alves</a>
    <li> <a href="https://github.com/EderRenato">Eder Renato</a>
    <li> <a href="https://github.com/marifariasz">Mariana Farias</a>
    <li> <a href="https://github.com/Beroradin">Matheus Pereira</a>
</ul>

## 🚀 Funcionalidades Principais
<ul>
    <li> <strong>Leitura de Sensores:</strong> Coleta dados de temperatura e umidade (AHT20) e pressão e altitude (BMP280).
    <li> <strong>Interface no Display:</strong> Exibe os dados em tempo real em um display OLED SSD1306.
    <li> <strong>Navegação em Múltiplas Telas:</strong> Um botão permite alternar entre três telas:
        <ol>
            <li> <strong>Tela Principal:</strong> Mostra todos os dados atuais (Temp, Umidade, Pressão, Altitude).
            <li> <strong>Tela de Limites:</strong> Exibe os valores mínimos e máximos configurados para os alarmes.
            <li> <strong>Tela de Histórico:</strong> Mostra a contagem de dados e a média de temperatura e umidade.
        </ol>
    <li> <strong>Sistema de Alarme:</strong> Monitora se as leituras estão fora dos limites pré-definidos (<code>config</code>).
        <ul>
            <li> <strong>Alerta Visual:</strong> Um LED RGB pisca em vermelho caso um alarme seja ativado.
            <li> <strong>Alerta Sonoro:</strong> Um buzzer emite bipes curtos durante o alarme.
            <li> <strong>Status OK:</strong> O LED RGB permanece verde quando tudo está dentro dos limites.
        </ul>
    <li> <strong>Registro de Histórico:</strong> Armazena as últimas 50 leituras em um buffer circular para cálculo de médias.
</ul>

## 🧠 Arquitetura Multicore
O projeto divide as tarefas de forma clara entre os dois núcleos do RP2040 para garantir que a aquisição de dados não seja interrompida pela atualização da interface, e vice-versa.

### Core 0 (Aquisição de Dados)
<ul>
    <li> <strong>Responsabilidade:</strong> Exclusivamente ler o sensores e processar os dados.
    <li> <strong>Tarefas:</strong>
    <ol>
        <li> Inicializa o barramento <code>i2c0</code> para comunicação com os sensores.
        <li> Realiza a leitura dos sensores AHT20 e BMP280 em um intervalo fixo (<code>UPDATE_INTERVAL_MS</code>).
        <li> Calcula a altitude com base na pressão.
        <li> Envia a estrutura <code>SensorData</code> (contendo todos os dados) para o Core 1 através da FIFO multicore.
    </ol>
</ul>`

### Core 1 (Interface do Usuário)
<ul>
    <li> <strong>Responsabilidade:</strong> Gerenciar toda a interação com o usuároi e periféricos de saída.
    <li> <strong>Tarefas:</strong>
    <ol>
        <li> Inicializa o barramento <code>i2c</code> para o display SSD1306.
        <li> Inicializa os GPIOs do botão, buzzer e LED RGB.
        <li> Aguarda a chegada de dados na FIFO vindos do Core 0.
        <li> Processa os dados recebidos (aplica offsets, adiciona ao histórico).
        <li> Verifica os alarmes (<code>check_alarms</code>) e controla o LED e o buzzer.
        <li> detecta pressionamentos de botão (<code>handle_buuttons</code>) para trocar de tela.
        <li> Atualiza o display OLED com as informações da tela atual (<code>update_display</code>).
    </ol>
</ul>

## ⚙️ Hardware Utilizado
<ul>
    <li> <strong>Microcontrolador:</strong> Raspberry Pi Pico W
    <li> <strong>Sensores (I2C):</strong>
    <ul>
        <li> AHT20 (Temperatura e Umidade)
        <li> BMP280 (Pressão e Temperatura)
    </ul>
    <li> <strong>Display (I2C):</strong>
    <ul>
        <li>OLED SSD1306 (128x64)
    </ul>
    <li> <strong>Saídas:</strong>
    <ul>
        <li> 1x LED RGB (cátodo ou Ânodo Comum)
        <li> 1x Buzzer
    </ul>
    <li> <strong>Entradas:</strong>
    <ul>
        <li> 1x Push-button (com resistor de pull-up interno).
    </ul>
</ul>

## 🔌 Mapeamento de Pinos (Pinout)
Conforme definido em <code>main.c</code>:
<table>
    <thead>
        <tr>
            <th>Componente</th><th>Pino</th><th>GPIO</th>
        <tr>
    </thead>
    <tbody>
        <tr>
            <td><strong> I2C 0 (Sensores)</strong></td><td>SDA</td><td>GPIO 0</td>
        </tr>
        <tr>
            <td></td><td>SCL</td><td>GPIO 1</td>
        </tr>
        <tr>
            <td><strong> I2C 1 (Display)</strong></td><td>SDA</td><td>GPIO 14</td>
        </tr>
        <tr>
            <td></td><td>SCL</td><td>GPIO 15</td>
        </tr>
        <tr>
            <td><strong>Entrada</strong></td><td>Botão A</td><td>GPIO 5</td>
        </tr>
        <tr>
            <td><strong>Saídas</strong></td><td>Buzzer</td><td>GPIO 21</td>
        </tr>
        <tr>
            <td></td><td>LED Vermelho</td><td>GPIO 13</td>
        </tr>
        <tr>
            <td></td><td>LED Verde</td><td>GPIO 11</td>
        </tr>
        <tr>
            <td></td><td>LED Azul</td><td>GPIO 12</td>
        </tr>
    </tbody>
</table>

## 🛠️ Como Compilar
Este projeto utiliza o <code>pico-sdk</code> e <code>cmake</code>.
1. **Clone o repositório:**

```bash
git clone https://github.com/EderRenato/Dual_Core_RP2040
cd Dual_Core_RP2040
```

2. **Inicialize os submódulos (pico-sdk):** Assumindo que o ```pico-sdk``` está incluído como um submódulo (prática comum).

```bash
git submodule update --init
```
Se o SDK estiver em outro local, ajuste a variável ````PICO_SDK_PATH```` no seu ambiente.

3. **Crie a pasta de build e compile**

```bash
mkdir build
cd build
cmake -G "Ninja" ..
ninja
```

5. **Carregue o arquivo:** Arraste o arquivo ``Dual_Core.uf2`` (ou o nome definido no ``CMakeLists.txt``) para o Raspberry Pi Pico no modo BOOTSEL.
