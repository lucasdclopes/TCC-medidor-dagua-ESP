# TCC medidor d'água

## Sobre

Este projeto é do TCC do curso de Engenheria de Computação da Univesp.

O Sistema trabalha com um sensor de distância ligado em um ESP32. Também há um módulo Relé ligado no ESP32 para o acionamento de uma bomba d'água.

Este repositório contém o código C para executar no dispositivo ESP32
O backend pode ser encontrado nesete outro repositório: https://github.com/lucasdclopes/TCC-medidor-dagua

## Hardware

Placa ESP: ESP32 ESP-WROOM-32 DEVKit V1

Sensor: Sensor de distância ultrassônico HC-SR04

Relé: Ideal Tronics MOD. RELE. 3V. 2CH.

Fonte de alimentação para 5v e 3.3v. Note que o relé precisa ser alimentado por 3.3v.

## Software

IDE: Arduino IDE 2.3.3

Deve-se ajustar a variável `serverAddr`, pois esta indica para onde o ESP32 enviará as requisições.

Ao ser iniciado, o dispositivo inicia um hotspot WiFi, você deve se conectar neste hotspot para configurar a conexão WiFi do dispositivo. Uma tela será aberta no browser com as redes disponíveis.

Após conectar ao WiFi, o dispositivo começa a obter as informações de distância e enviá-las para o backend por meio de requisições HTTP com os dados em um payload JSON. O servidor responde com parâmetros de configuração, que são a frequência com que as medições devem ser obtidas, e quando a bomba d'água deve ser acionada. 
O sistema conta com algumas proteções para que não seja acionado muitas vezes por segundo. 
