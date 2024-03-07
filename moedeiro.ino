 /* Projeto: Monitoramento Remoto de moedeiros
 * SMAP - Soluções em Melhorias e Aprimoramento de Processos
 * www.smap.tech
 * Data: 07/03/2024
 * 1. Ler os pulsos do moedeiro
 * 2. Calcular valor
 * 3. Enviar valor por MQTT
 */

//++++++++++Bibliotecas+++++++++++
#include <WiFiManager.h> // Biblioteca para gerenciar conexões WiFi
#include <WiFi.h> // Biblioteca para cliente WiFi
#include <PubSubClient.h> // Biblioteca MQTT
#include "nvs_flash.h" // Biblioteca Non-Volatille Storage
#include "DHTesp.h" // Biblioteca DHT

//++++++++++Variáveis+++++++++++
//const char* mqtt_server = "ec2-18-231-110-161.sa-east-1.compute.amazonaws.com"; // Endereço do servidor AWS
const char* mqtt_server = "demo.thingsboard.io"; // Endereço do servidor AWS
#define TOKEN "ts4u1pgw4vlr7wkljm1l"
#define MSG_BUFFER_SIZE (150) // Tamanho de buffer
char msg[MSG_BUFFER_SIZE]; // String msg
int try_reconnect_mqtt = 0;

const int pinPulse = 2; // Pino do pulso
float valor_real = 0.0; // Variavel valor
float tensaoBat = 0.0; // Tensão da bateria
float latitude = -3.4355535793558856; 
float longitude = -39.14619310619749;

volatile int pulseCount = 0; // Variável volátil para armazenar o número de pulsos

void IRAM_ATTR pulseCounter() {
  pulseCount++; // Incrementa o contador de pulsos quando uma interrupção é acionada
}

#define CHAVE_NVS  "montante"

//+++++++++Objetos+++++++++++++
WiFiClient espClient; // Conexão WiFi
PubSubClient client(espClient); // Conexão MQTT
DHTesp dht; // Sensor DHT

//+++++++++Funções++++++++++++
void grava_dado_nvs(uint32_t dado);
uint32_t le_dado_nvs(void);
 
/* Função: grava na NVS um dado do tipo interio 32-bits
 *         sem sinal, na chave definida em CHAVE_NVS
 * Parâmetros: dado a ser gravado
 * Retorno: nenhum
 */
void grava_dado_nvs(uint32_t dado)
{
    nvs_handle handler_particao_nvs;
    esp_err_t err;
    
    err = nvs_flash_init_partition("nvs");
     
    if (err != ESP_OK)
    {
        Serial.println("[ERRO] Falha ao iniciar partição NVS.");           
        return;
    }
 
    err = nvs_open_from_partition("nvs", "ns_nvs", NVS_READWRITE, &handler_particao_nvs);
    if (err != ESP_OK)
    {
        Serial.println("[ERRO] Falha ao abrir NVS como escrita/leitura"); 
        return;
    }
 
    /* Atualiza valor do horimetro total */
    err = nvs_set_u32(handler_particao_nvs, CHAVE_NVS, dado);
 
    if (err != ESP_OK)
    {
        Serial.println("[ERRO] Erro ao gravar horimetro");                   
        nvs_close(handler_particao_nvs);
        return;
    }
    else
    {
        Serial.println("Dado gravado com sucesso!");     
        nvs_commit(handler_particao_nvs);    
        nvs_close(handler_particao_nvs);      
    }
}
 
/* Função: le da NVS um dado do tipo interio 32-bits
 *         sem sinal, contido na chave definida em CHAVE_NVS
 * Parâmetros: nenhum
 * Retorno: dado lido
 */
uint32_t le_dado_nvs(void)
{
    nvs_handle handler_particao_nvs;
    esp_err_t err;
    uint32_t dado_lido;
     
    err = nvs_flash_init_partition("nvs");
     
    if (err != ESP_OK)
    {
        Serial.println("[ERRO] Falha ao iniciar partição NVS.");         
        return 0;
    }
 
    err = nvs_open_from_partition("nvs", "ns_nvs", NVS_READWRITE, &handler_particao_nvs);
    if (err != ESP_OK)
    {
        Serial.println("[ERRO] Falha ao abrir NVS como escrita/leitura");         
        return 0;
    }
 
    /* Faz a leitura do dado associado a chave definida em CHAVE_NVS */
    err = nvs_get_u32(handler_particao_nvs, CHAVE_NVS, &dado_lido);
     
    if (err != ESP_OK)
    {
        Serial.println("[ERRO] Falha ao fazer leitura do dado");         
        return 0;
    }
    else
    {
        Serial.println("Dado lido com sucesso!");  
        nvs_close(handler_particao_nvs);   
        return dado_lido;
    }
}

// Reconecta ao servidor MQTT
void reconnect() {
  int try_reconnect_mqtt=0;
  // Enquanto não tiver conectado
  while (!client.connected()) {
    try_reconnect_mqtt++;
    Serial.print("try_reconnect_mqtt: ");
    Serial.println(try_reconnect_mqtt);
    portYIELD_FROM_ISR();
    Serial.print("Tentando conexão MQTT...");
    // Cria ID randomico
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    // Tentando conexão MQTT
    if (client.connect(clientId.c_str(), TOKEN, NULL)) {
      Serial.println("Conectado");
      try_reconnect_mqtt = 0;
    } else {
      Serial.print("Erro de codigo = ");
      Serial.print(client.state());
      Serial.println("Tentanto novamente em 5 segundos");
      delay(5000);
    }
  }
}

// Funçao para verificar conexão WiFi
static void verificaConexao(void *){
  while (1) {
    int reads=0;
    while(WiFi.status() != WL_CONNECTED){
      Serial.println("Sem conexão WiFi!");
      reads++;
      if(reads > 2000){
        Serial.printf("Salvando valor acumulado: %.2f\n", valor_real);
//      Serial.println(valor_real);
        grava_dado_nvs(valor_real);
        Serial.println("Sem conexão. Resetando!");
        ESP.restart();
      }
      vTaskDelay(30000 / portTICK_PERIOD_MS);
    }
    if(try_reconnect_mqtt > 360){
      Serial.println("Sem conexão com servidor MQTT!");
      Serial.printf("Salvando valor acumulado: %.2f\n", valor_real);
      grava_dado_nvs(valor_real);
      Serial.println("Sem conexão. Resetando!");
      ESP.restart();
    }
    portYIELD_FROM_ISR();
    vTaskDelay(3600000 / portTICK_PERIOD_MS);
  }
}
static void leitura_valor(void *){
  //Le variavel valor na NVS
  valor_real = le_dado_nvs();
  //Serial.print("Valor acumulado lido da NVS: ");
  //Serial.println(valor);
  while (1) {
    pulseCount++;
    Serial.print("Pulsos: ");Serial.println(pulseCount);
    if(true){
      // Calcula valor_real
      valor_real = pulseCount*1.0;
      // Lê tensão da bateria
      tensaoBat = analogRead(34)*3.3*2.115/4096;
      Serial.print("Tensão: ");Serial.println(tensaoBat);
        
      // Prepara, printa e publica payload
      snprintf (msg, MSG_BUFFER_SIZE, "{\"Pulsos\":%d,\"Valor\":%.2f,\"Bateria\":%.2f,\"latitude\":%.7f,\"longitude\":%.7f}", pulseCount, valor_real, tensaoBat, latitude, longitude);
      Serial.print("Payload: ");
      Serial.println(msg);
      client.publish("v1/devices/me/telemetry", msg);
      //Serial.println("Entrou no if");
    }
    portYIELD_FROM_ISR();
    vTaskDelay(60000 / portTICK_PERIOD_MS); // Aguarda 1 minuto
  }
}
void setup() {
  Serial.begin(9600);
  grava_dado_nvs(0); // Grava valor inicial na flash  
  pinMode(pinPulse, INPUT_PULLUP); // Configura o pino de entrada para detectar pulsos
  attachInterrupt(digitalPinToInterrupt(pinPulse), pulseCounter, RISING); // Configura a interrupção para detectar borda de subida
  
  WiFi.mode(WIFI_STA);
  WiFiManager wm; // Objeto wifimanager  
  bool res = wm.autoConnect("SMAP","password"); // Tenta conectar com credencial salva
  
  // Caso conecte com sucesso   
  Serial.println("Conectado!");
  client.setServer(mqtt_server, 1883); // Conecta com o servidor MQTT

  // Tarefa de verificar a conexão
  xTaskCreatePinnedToCore(verificaConexao, "Conexao", 2048, NULL, 20, NULL, 0);
  // Tarefa para realizar leitura do moedeiro
  xTaskCreate(leitura_valor, "leituraVazao", 2048, NULL, 20, NULL);
}

void loop() {
  // Seu código principal aqui
  if (!client.connected()) { // Caso não esteja conectado ao servidor MQTT
    reconnect(); // Reconecta
  }
  client.loop(); // Loop do cliente MQTT
  portYIELD_FROM_ISR(); // Feedback do whatchdog
}
