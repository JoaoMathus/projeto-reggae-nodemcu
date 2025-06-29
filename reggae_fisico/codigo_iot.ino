#include <ESP8266WiFi.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <SPI.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

/* Constantes para conexão externa */
const char* ssid = "***"; /* segredo */
const char* senha_do_wifi = "***"; /* segredo */
const char* servidor_mqtt = ""; /* segredo */
const char* usuario_mqtt = ""; /* segredo */
const char* senha_mqtt = ""; /* segredo */
const int porta_mqtt = -1; /* segredo */
const char* topico_de_envio = "dados_coletados";
String device_id = String();
/* ------------------------------ */

/* Cliente UDP para ter acesso a tempo */
WiFiUDP ntpUDP;
NTPClient cliente_tempo(ntpUDP, "pool.ntp.org");
time_t tempo_atual = 0; /* para sincronizar elementos do código */
bool primeira_vez_executando_depois_de_ligar = true;
/* ----------------------------------- */

/* WiFi */
WiFiClientSecure cliente_esp;
PubSubClient cliente(cliente_esp);
/* ---- */

/* Mensagem MQTT */
unsigned long ultima_mensagem = 0;
#define TAMANHO_DO_BUFFER_DE_MENSAGEM 50
char mensagem[TAMANHO_DO_BUFFER_DE_MENSAGEM];
/* ------------- */

/* Sensor de temperatura e umidade do ar */
#define PINO_DO_DHT 2 /* D4 */
#define TIPO_DO_DHT DHT11

DHT dht(PINO_DO_DHT, TIPO_DO_DHT);
/* ------------------------------------- */

/* Display OLED */
#define LARGURA_DA_TELA 128
#define ALTURA_DA_TELA 64
#define ENDERECO_I2C_DO_OLED 0x3C

Adafruit_SH1106G display(LARGURA_DA_TELA, ALTURA_DA_TELA, &Wire, -1);
/* ------------ */

/* De quanto em quanto tempo enviar mensagem MQTT */
#define INTERVALO_DE_ENVIO 15.0
time_t tempo_do_ultimo_envio = 0; /* calcular a diferença com tempo_atual */
bool deve_enviar_mensagem = false;
/* ---------------------------------------------- */

/* Temporização de exposição ao sol */
bool sinal_de_acionamento_do_temporizador = true; /* MQTT */
int segundos_ao_sol = 61;
bool terminou_exposicao_ao_sol = false;
/* -------------------------------- */

/* Sobre rega e água */
bool deve_regar = true;
time_t horario_que_foi_regado = 0;
bool reservatorio_vazio = false;
int numero_de_vezes_para_regar = 3;
/* ----------------- */

/* Sobre temperatura */
float temperatura_do_ar = 0;
float temperatura_maxima_recomendada = 40.0f; /* MQTT */
bool temperatura_maxima_excedida = false;
float temperatura_minima_recomendada = 10.0f; /* MQTT */
bool temperatura_minima_excedida = false;
/* ----------------- */

/* Sensor de umidade do solo */
#define LIMIAR_DA_UMIDADE_DO_SOLO 50 /* % */
int valor_do_sensor_no_ar = 620;
int valor_do_sensor_na_agua = 340;
int porcentagem_da_umidade_do_solo = 0;
/* ------------------------- */

/* Bomba d'água (simulada por um LED) */
const int pino_da_bomba_agua = 14; /* D5 */
/* ---------------------------------- */

void setup() {
	Serial.begin(115200);

	pinMode(pino_da_bomba_agua, OUTPUT);
	digitalWrite(pino_da_bomba_agua, LOW);

	dht.begin();

	if (!display.begin(ENDERECO_I2C_DO_OLED)) {
		Serial.println("Mostrador OLED falhou.");
		while (1)
			;
	}

	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(SH110X_WHITE);
	display.display();

	WiFi.begin(ssid, senha_do_wifi);
	mostrar_aviso("Tentando conectar ao WiFi...");
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
	}

	mostrar_aviso("Conectado a: " + String(ssid));
	delay(3000);

	cliente.setServer(servidor_mqtt, porta_mqtt);
	cliente_esp.setInsecure();
	cliente.setCallback(callback);

	/* Inicializando o cliente NTP */
	cliente_tempo.begin();
	cliente_tempo.setTimeOffset(-10800); /* -3, horário de Brasília */
	/* --------------------------- */
}

void loop() {
	if (!cliente.connected()) reconectar();

	/* Atualizando o que tem que atualizar */
	cliente.loop();
	cliente_tempo.update();
	/* ----------------------------------- */

	/* Atualizando o tempo atual */
	tempo_atual = cliente_tempo.getEpochTime();
	/* ------------------------- */

	/* Foi necessário...
		 Não dá para colocar no setup(), porque o tempo não foi atualizado (update())
	 */
	if (primeira_vez_executando_depois_de_ligar) {
		/* Inicializando */
		tempo_do_ultimo_envio = cliente_tempo.getEpochTime();
		horario_que_foi_regado = cliente_tempo.getEpochTime();
		primeira_vez_executando_depois_de_ligar = false;
		/* ------------  */
	}

	/* Coisas relacionadas ao tempo */
	/* Temporizando o envio para o frontend por MQTT */
	if (difftime(tempo_atual, tempo_do_ultimo_envio) >= INTERVALO_DE_ENVIO) {
		deve_enviar_mensagem = true;
	}
	/* --------------------------------------------- */
	/* ---------------------------- */

	/* Porcentagem de umidade */
	porcentagem_da_umidade_do_solo = ler_porcentagem_da_umidade();

	if (porcentagem_da_umidade_do_solo < 0) {
		porcentagem_da_umidade_do_solo = 0;
	}

	if (porcentagem_da_umidade_do_solo > 100) {
		porcentagem_da_umidade_do_solo = 100;
	}
	/* --------------------- */

	/* Lendo a temperatura */
	temperatura_do_ar = ler_temperatura();
	/* ------------------- */
	
	if (sinal_de_acionamento_do_temporizador) {
		if (segundos_ao_sol <= 0) {
			terminou_exposicao_ao_sol = true;
			sinal_de_acionamento_do_temporizador = false;
		}
		segundos_ao_sol--;
	}

	if (temperatura_do_ar > temperatura_maxima_recomendada) {
		tratar_temperatura_maxima_excedida();
	}

	if (temperatura_do_ar < temperatura_minima_recomendada) {
		tratar_temperatura_minima_excedida();
	}

	if (deve_regar) {
		procedimento_de_rega();
		if (numero_de_vezes_para_regar <= 1) {
			deve_regar = false;
		}
		numero_de_vezes_para_regar--;
	}

	if (deve_enviar_mensagem) {
		int segundos_desde_a_rega = pegar_segundos_desde_a_ultima_rega();
		int minutos_desde_a_rega = segundos_para_minutos(segundos_desde_a_rega);

		publicar_dados(
			temperatura_do_ar,
			porcentagem_da_umidade_do_solo,
	    terminou_exposicao_ao_sol,
			temperatura_maxima_excedida,
			temperatura_minima_excedida,
			minutos_desde_a_rega,
			reservatorio_vazio);
		
		deve_enviar_mensagem = false;
		tempo_do_ultimo_envio = cliente_tempo.getEpochTime();
	}

	mostrar_dados(temperatura_do_ar, porcentagem_da_umidade_do_solo);
	delay(1000);
}

/* Reconectar sempre que cair a conexão */
void reconectar() {
	while (!cliente.connected()) {
		mostrar_aviso("Tentando conectar ao Broker...");
		String id_cliente = "ESP8266Client-";
		id_cliente += String(random(0xffff), HEX);

		if (cliente.connect(id_cliente.c_str(), usuario_mqtt, senha_mqtt)) {
			mostrar_aviso("Conectado com sucesso");
			delay(2000);
			if (device_id.length() < 1) {
				cliente.subscribe("dados");
				mostrar_aviso("Assinou [dados]");
			} else {
				cliente.subscribe(String(String("dados_") + device_id).c_str());
				mostrar_aviso("Assinou [dados_" + String(device_id) +"]");
			}
			delay(2000);
		} else {
			for (int i = 9; i >= 0; i--) {
				mostrar_aviso("Tentando novamente em " + String(i) + " segundos...");
				delay(1000);
			}
		}
	}
}

/* Tratando as mensagens recebidas */
void callback(char* topico, byte* payload, unsigned int largura) {
	String mensagem_que_chega = "";

	for (int i = 0; i < largura; ++i) mensagem_que_chega += (char)payload[i];

	mostrar_aviso("Mensagem de [" + String(topico) +"]");
	delay(2000);

	if (strcmp(topico, "dados") == 0) {
		JsonDocument doc;
		deserializeJson(doc, mensagem_que_chega.c_str());
		segundos_ao_sol = minutos_para_segundos(horas_para_minutos(doc["sun"]));
		temperatura_minima_recomendada = doc["min"];
		temperatura_maxima_recomendada = doc["max"];
		numero_de_vezes_para_regar = doc["irrigation"];
		device_id = String(doc["deviceId"]);
		cliente.unsubscribe("dados");
		cliente.subscribe(String(String("dados_") + device_id).c_str());
	}

	delay(3000);
}

/* Publicando mensagem */
void publicar_mensagem(const char* topico, String payload, boolean retido) {
	if (cliente.publish(topico, payload.c_str(), true)) {
		mostrar_aviso("Mensagem enviada para ["+ String(topico)+"]");
		delay(3000);
	}
}

int pegar_segundos_desde_a_ultima_rega() {
	return difftime(tempo_atual, horario_que_foi_regado);
}

/* Conversão de tempo */
int horas_para_minutos(int horas) {
	return horas * 60;
}

int minutos_para_segundos(int minutos) {
	return minutos * 60;
}

int segundos_para_minutos(int segundos) {
	return segundos / 60;
}

int minutos_para_horas(int minutos) {
	return minutos / 60;
}
/* ------------------ */

/* Mostra temperatura, umidade e tempo de exposição ao sol (no futuro) */
void mostrar_dados(float temperatura, int porcentagem_da_umidade) {
	display.clearDisplay();
	display.setCursor(0, 10);
	display.println(cliente_tempo.getFormattedTime());
	display.setCursor(0, 20);
	display.println("Temp: " + String(temperatura) + " C");
	display.setCursor(0, 30);
	display.println("Solo " + String((porcentagem_da_umidade > LIMIAR_DA_UMIDADE_DO_SOLO) ? "molhado" : "seco"));
	if (numero_de_vezes_para_regar > 0) {
		display.setCursor(0, 40);
		display.println("Regas restantes: " + String(numero_de_vezes_para_regar));
	}
	if (sinal_de_acionamento_do_temporizador) {
		String tempo_restante = "";

		if (segundos_ao_sol < 60) {
			tempo_restante = String(segundos_ao_sol) + " sec";
		} else {
			int minutos = segundos_para_minutos(segundos_ao_sol);
			if (minutos < 60) {
				tempo_restante = String(minutos) + " min";
			} else {
				int horas = minutos_para_horas(minutos);
				tempo_restante = String(horas) + " h";
			}
		}

		display.setCursor(0, 50);
		display.println("No sol ate: " + tempo_restante);
	}
	display.display();
}

/* Faz o log no display OLED */
void mostrar_aviso(String aviso) {
	display.clearDisplay();
	display.setCursor(0, 10);
	display.println(aviso);
	display.display();
}

/* Simula a rega da planta */
void regar_a_planta() {
	digitalWrite(pino_da_bomba_agua, HIGH);
	mostrar_aviso("Regando...");
	delay(3000);
	digitalWrite(pino_da_bomba_agua, LOW);
	horario_que_foi_regado = cliente_tempo.getEpochTime();
}

/* Lê e converte em porcentagem o dado do sensor de umidade do solo */
int ler_porcentagem_da_umidade() {
	int umidade_do_solo = analogRead(A0);
	return map(umidade_do_solo, valor_do_sensor_no_ar, valor_do_sensor_na_agua, 0, 100);
}

void procedimento_de_rega() {
	int umidade_anterior = porcentagem_da_umidade_do_solo;
	regar_a_planta();
	int umidade_posterior = ler_porcentagem_da_umidade();
	int tolerancia = 10;
	if (umidade_anterior >= umidade_posterior + tolerancia) {
		reservatorio_vazio = true; /* não há água */
		mostrar_aviso("Sem agua...");
		delay(2000);
	} else {
		reservatorio_vazio = false;
	}
}

void publicar_dados(
	float temperatura_do_ar,
	int porcentagem_da_umidade_do_solo,
	bool terminou_exposicao_ao_sol,
	bool temperatura_maxima_excedida,
	bool temperatura_minima_excedida,
	int minutos_desde_a_ultima_rega,
	bool reservatorio_vazio
	) {
	DynamicJsonDocument doc(1024);
	doc["deviceId"] = device_id.c_str();
	doc["siteId"] = "Dados coletados";
	doc["temperature"] = temperatura_do_ar;
	doc["humidity"] = porcentagem_da_umidade_do_solo;
	doc["sun_expo_ended"] = terminou_exposicao_ao_sol;
	doc["max_temp_exceeded"] = temperatura_maxima_excedida;
	doc["min_temp_exceeded"] = temperatura_minima_excedida;
	doc["min_since_watered"] = minutos_desde_a_ultima_rega;
	doc["is_reservatory_empty"] = reservatorio_vazio;

	char mensagem_mqtt[256];
	serializeJson(doc, mensagem_mqtt);

	if (device_id.length() < 1) {
		publicar_mensagem(topico_de_envio, mensagem_mqtt, true);
	} else {
		publicar_mensagem(String(String(topico_de_envio) + "_" + device_id).c_str(), mensagem_mqtt, true);
	}
	terminou_exposicao_ao_sol = false;
	temperatura_maxima_excedida = false;
	temperatura_minima_excedida = false;
}

void tratar_temperatura_maxima_excedida() {
	mostrar_aviso("Temperatura alta demais");
	temperatura_maxima_excedida = true;
	delay(2000);
}

void tratar_temperatura_minima_excedida() {
	mostrar_aviso("Temperatura baixa demais");
	temperatura_minima_excedida = true;
	delay(2000);
}

float ler_temperatura() {
	float temperatura = dht.readTemperature();

	while (isnan(temperatura)) {
		mostrar_aviso("Lendo temperatura. Se estiver preso aqui, verique o circuito.");
		delay(100);
		temperatura = dht.readTemperature();
	}

	return temperatura;
}