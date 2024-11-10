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
//const char *ssid = "seu ssid";                                           
//senha da rede WIFI
//const char *password = "sua senha";

//Endereço do servidor da Amazon, que é responsável por armazenar os logs e disparar os alertas
const char *serverAddr = "http://54.207.176.200:8080/tcc-medidor-dagua/api/medicao"; 

const int rele_in1 = 4;

//armazena, em milésimos de segundos, a quanto tempo o programa foi executado
unsigned int lastTime = 0;
//em milésimos de segundos, a frequência máxima com que o programa pode ser executado. Serve como segurança para nunca ser menor que 1 segundos.
unsigned int timer_Delay_Minimo = 1000;
//frequência máxima com que o programa pode ser executado
unsigned int timerDelay = timer_Delay_Minimo;

//quanto tempo faz que o relé foi desligado ou ligado (ms). Usado para evitar que o relé fique ligando e desligando toda hora
unsigned int rele_ultimaAlteracao = 0;
//em milésimos de segundos, a frequência máxima com que o estado do relé pode ser alterado.
unsigned int rele_intervalo_Minimo = 7000;

//Inicializa o sensor hipersônico, definindo quais os pinos de comunicação
//Faz isto fora do setup pois preciso utilizar a referencia ao objeto no loop
Ultrasonic ultrasonic(TRIGGER_PIN, ECHO_PIN);

bool primeiraExecucao = true;

//Este método é executado quando o dispositivo é ligado
void setup(void) {
  
  bool primeiraExecucao = true;

  //Inicializa a comunicação com a porta serial 115200, 
  //caso esteja ligado em um computador 
  Serial.begin(115200);

  //Inicializa o relé
  pinMode(rele_in1, OUTPUT);
  
  //Gerenciador de wifi, para não ter que usar rede e senha hardcoded
  WiFiManager wifiManager;
  wifiManager.autoConnect("ESP32-TCC-UNVP");

  //Escreve na porta serial o IP com que se conectou na rede wifi
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

//Este método é executado após o fim do setup. Sempre que é finalizado, é executado novamente.
void loop(void) {

  //pausa o processamento por alguns instantes para evitar o consumo desnecessário de recursos
  delay(100);

  if (primeiraExecucao) {
    digitalWrite(rele_in1, HIGH);
    primeiraExecucao = false;
  }
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

  //leitura da distância obtida pela sensor hipersônico 
  int distancia = ultrasonic.read(CM); 

  /*
  //código para debugar na porta serial o valor do sensor. Desnecessário na "produção"
  char strDistancia[5] = "";
  snprintf(strDistancia,sizeof(strDistancia),"%d",distancia);
  Serial.println(strDistancia);
  */
  
  //Configura o cliente HTTP, que irá se comunicar com a aplicação Java
  HTTPClient http;
  http.begin(serverAddr); //serverAddr contém o endereço da aplicação

  //Configura os cabeçalhos da requisição HTTP, para iinformar que é uma requisição com um payload do tipo JSON
  http.addHeader("Content-Type", "application/json");

  //Variável que armazenará o JSON. Esta pode armazenar, no máximo 60 caracteres. É mais do que o suficiente para o payload que será montado.
  char json[60] = "";

  /*
    Monta a String com o payload e o armazena na variável json, usando a função snprintf
    %d é o tipo de formatação. Indica que o valor é do tipo int de base 10
    O Json ficará, por exemplo, desta forma: {"vlDistancia": 21 }
  */
  snprintf(json,sizeof(json),"{\"vlDistancia\": %d }",distancia);
  
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

    configurarRele(distancia,nivelRele);

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


void configurarRele(float distancia, float nivelRele){

  /*
  Verifica quanto tempo faz que o relé alterou de estaedo. Não executa o método se passou pouco tempo 
  */
  if (!((millis() - rele_ultimaAlteracao) > rele_intervalo_Minimo)) 
    return;

  int nivelAcionamentoRele = nivelRele;
  if (nivelAcionamentoRele == -1) //-1 indica que não tem alerta para acionar o dispositivo, sai do método
    return;

  //Lê o estado atual do rele
  int releState = digitalRead(rele_in1);

  //se a distancia do sensor até a água é maior que o nível critico, 
  //então entra na lógica que vai ligar o relé. Ligando, assim, a bomba
  //caso contrário, entra na lógica que vai desligar a bomba
  if ( distancia > nivelAcionamentoRele ){ 
    if (releState == LOW) //já está LOW, nem roda o write
      return; 
    digitalWrite(rele_in1, LOW);
    rele_ultimaAlteracao = millis();
    Serial.println("Corrente passando"); //debug
  } else {
    if (releState == HIGH) //já está HIGH, nem roda o write
      return; 
    //bota uma margem de 3 cm antes de desligar o relé, pra evitar ficar ligando / desligando constantemente por pequenas variações de nível
    if (distancia < nivelAcionamentoRele - 3) {
      digitalWrite(rele_in1, HIGH);
      rele_ultimaAlteracao = millis();
      Serial.println("Sem corrente passando"); //debug
    }
  }

}
