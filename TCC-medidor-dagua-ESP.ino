#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Ultrasonic.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager 

#define ECHO_PIN 18
#define TRIGGER_PIN 5

//Configurações do WIFI
//nome da rede WIFI
//const char *ssid = "bacon";
//senha da rede WIFI
//const char *password = "Chedd4r!";

//Endereço do servidor da Amazon, que é responsável por armazenar os logs e disparar os alertas
const char *serverAddr = "http://192.168.15.53:8080/tcc-medidor-dagua/api/medicao"; 

const int relay_in1 = 4;

//armazena, em milésimos de segundos, a quanto tempo o programa foi executado
unsigned int lastTime = 0;
//em milésimos de segundos, a frequência máxima com que o programa pode ser executado. Serve como segurança para nunca ser menor que 1 segundos.
unsigned int timer_Delay_Minimo = 1000;
//frequência máxima com que o programa pode ser executado
unsigned int timerDelay = timer_Delay_Minimo;

//quanto tempo faz que o relay foi desligado ou ligado (ms). Usado para evitar que o relay fique ligando e desligando toda hora
unsigned int relay_ultimaAlteracao = 0;
//em milésimos de segundos, a frequência máxima com que o estado do relay pode ser alterado.
unsigned int relay_intervalo_Minimo = 5000;

//inicializa o sensor ultrassonico
Ultrasonic ultrasonic(TRIGGER_PIN, ECHO_PIN);

//Este método é executado quando o dispositivo é ligado
void setup(void) {

  //Inicializa a comunicação com a porta serial 115200, caso esteja ligado em um computador 
  Serial.begin(115200);

  //Inicializa o relé com este desligado
  digitalWrite(relay_in1, HIGH);
  pinMode(relay_in1, OUTPUT);
  
    //Gerenciador de wifi, para não ter que usar hardcoded
  WiFiManager wifiManager;
  wifiManager.autoConnect("ESP32-PI6");

  //Escreve na porta serial o IP com que se conectou na rede wifi
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

//Este método é executado após o fim do setup. Sempre que é finalizado, é executado novamente.
void loop(void) {

  /*
  Verifica quanto tempo faz que a última execução foi feita. Se o tempo não for maior que a frequência máxima, encerra a execução do método.
  Isto é feito para evitar uma sobrecarga no servidor e no sensor
  */
  if (!((millis() - lastTime) > timerDelay)) 
    return;

  //Verifica status do WiFi. Só prossegue a execução se estiver conectado
  if(!(WiFi.status()== WL_CONNECTED)){
    Serial.println("WiFi Disconnected");
    return;
  }

  //float distancia = ultrasonic.afstandCM();
  float distancia = ultrasonic.read(CM); 
  char strDistancia[5] = "";
  snprintf(strDistancia,sizeof(strDistancia),"%.2f",distancia);
  Serial.println(strDistancia);
  
  //Declara o HTTPClient. Este objeto serve para executar requisições HTTP
  HTTPClient http;
  
  //Configura o endereço do servidor
  http.begin(serverAddr);

  //Configura os cabeçalhos da requisição HTTP. No caso, para informar que é uma requisição com um payload do tipo JSON
  http.addHeader("Content-Type", "application/json");

  //Variável que armazenará o JSON. Esta pode armazenar, no máximo 60 caracteres. É mais do que o suficiente para o payload que será montado.
  char json[60] = "";

  /*
    Monta a String com o payload e o armazena na variável json, usando a função snprintf
    %.2f é o tipo de formatação. Indica que o valor é do tipo float, com duas casas decimais
    O Json ficará, por exemplo, desta forma: {"vlDistancia": 10.55 }
  */
  snprintf(json,sizeof(json),"{\"vlDistancia\": %.2f }",distancia);
  
  //Envia uma requisição HTTP, com verbo POST, para o servidor, contendo o payload JSON montando anteriormente, aguarda a resposta do servidor.
  Serial.println("Invocando WS");
  int httpResponseCode = http.POST(json);
  
  //A aplicação no servidor responde com o código HTTP 201 se houver sucesso no recebimento das informações. Portanto, verifica se a resposta foi 201.
  if (httpResponseCode==201) { 

    JsonDocument doc;
    processarResponseServidorOK(http,doc);
    /*
    Além do código de resposta, a aplicação no servidor responde com um JSON com duas propriedades:
    1 - intervalo, que é a frequência de execução deste programa, em milésimos de segundos. Desta forma é possível que o usuário, por meio da aplicação do servidor, 
    configure esta frequência, não sendo necessário alterar o programa que roda no dispostivo
    2 - vlAcionamento. Valor de acionamento da bomba dágua. Se a distancia medida até a agua for maior do que este valor, liga o relé para ligar a bomba
    */
    timerDelay = doc["intervalo"];
    float nivelRele = doc["vlAcionamento"];

    Serial.println("timerDelay: ");
    Serial.println(timerDelay);

    setarRelay(distancia,nivelRele);

  } else {
    Serial.println("Response com erro: ");
    Serial.println(httpResponseCode);
  }

  //Finaliza a comunicação http. É necessário para evitar que a conexão com o servidor fique aberta indefinidamente, o que pode tomar muitos recursos do servidor e do dispositivo.
  http.end();

    /*
  Aqui verifica se a frequência configurada é menor que a frequência máxima. Se for, assume a frequência máxima ao invés da configurada. 
  É uma proteção contra uma configuração imprópria
  */
  if (timerDelay < timer_Delay_Minimo) 
    timerDelay = timer_Delay_Minimo; 
  
  //Carrega o momento em que a execução foi executada. 
  lastTime = millis();
  
}

//faz o processamento do retorno do servidor. Como http é um objeto, deve-se passar uma referencia com o keyword &
void processarResponseServidorOK(HTTPClient &http, JsonDocument &doc){
  Serial.println("Response com 201");

  //carrega os dados da resposta e converte para char
  String wsResponse = "";
  wsResponse = http.getString();
  int responseLen = 1;
  responseLen = wsResponse.length();
  char responseData[responseLen+1]; 
  wsResponse.toCharArray(responseData, responseLen);

  //transforma a resposta do servidor no objeto json
  deserializeJson(doc, responseData);
}


void setarRelay(float distancia, float nivelRele){

  /*
  Verifica quanto tempo faz que o relay alterou de estaedo. Não executa o método se passou pouco tempo 
  */
  if (!((millis() - relay_ultimaAlteracao) > relay_intervalo_Minimo)) 
    return;

  Serial.println("nivelRele: ");
  Serial.println(nivelRele);

  int nivelAcionamentoRele = nivelRele;
  if (nivelAcionamentoRele == -1) //-1 indica que não tem alerta para acionar o dispositivo, sai do método
    return;

  int relayState = digitalRead(relay_in1);
  Serial.println("relayState: ");
  Serial.println(relayState);
  if ( distancia > nivelAcionamentoRele ){
    if (relayState == LOW) //já está LOW, nem roda o write
      return; 
    digitalWrite(relay_in1, LOW);
    relay_ultimaAlteracao = millis();
    Serial.println("Corrente passando");
  } else {
    if (relayState == HIGH) //já está HIGH, nem roda o write
      return; 
    //bota uma margem de 4 cm antes de desligar o relé, pra evitar ficar ligando / desligando constantemente por pequenas variações de nível
    if (distancia < nivelAcionamentoRele - 4) {
      digitalWrite(relay_in1, HIGH);
      relay_ultimaAlteracao = millis();
      Serial.println("Sem corrente passando");
    }
  }

}