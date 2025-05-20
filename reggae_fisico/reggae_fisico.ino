/*
 O seguintes dados virão por MQTT:
  sinal_de_acionamento_do_temporizador;
	segundos_ao_sol;
	temperatura_maxima_recomendada;
	temperatura_minima_recomendada; e
	se a planta/fruta deve ser coletada.
*/

#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

/* Temporário */
#define LIMIAR_DA_UMIDADE_DO_SOLO 500

#define PINO_DO_DHT D4
#define TIPO_DO_DHT DHT11

#define LARGURA_DA_TELA 128
#define ALTURA_DA_TELA 64
#define ENDERECO_I2C_DO_OLED 0x3C

DHT dht(PINO_DO_DHT, TIPO_DO_DHT);

Adafruit_SH1106G display(LARGURA_DA_TELA, ALTURA_DA_TELA, &Wire, -1);

bool sinal_de_acionamento_do_temporizador = true; /* valor temporário */
int segundos_ao_sol = 3; /* valor temporário */
float temperatura_maxima_recomendada = 40.0f; /* valor temporário */
float temperatura_minima_recomendada = 10.0f; /* valor temporário */

String avisos[] = {
	"Reservatorio vazio",
	"Fim do banho de sol",
	"Temperatura alta demais",
	"Temperatura baixa demais"
};
/* Índices para o vetor de avisos */
const int aviso_reservatorio_vazio = 0;
const int aviso_fim_de_exposicao_ao_sol = 1;
const int aviso_temperatura_alta = 2;
const int aviso_temperatura_baixa = 3;

void setup() {
	Serial.begin(115200);

	dht.begin();

	if (!display.begin(ENDERECO_I2C_DO_OLED)) {
		Serial.println("Deu errado a inicializacao do OLED");
		while (1)
			;
	}

	display.clearDisplay();

	display.setTextSize(1);
	display.setTextColor(SH110X_WHITE);
	display.display();
}

int segundos_para_milissegundos(int segundos) {
	return segundos * 1000; /* 1s = 1000ms */
}

void mostrar_temperatura_e_umidade(float temperatura, int umidade) {
	display.clearDisplay();
	display.setCursor(0, 10);
	display.println("Temperatura: " + String(temperatura) + " C");
	display.setCursor(0, 20);
	display.println("Solo: " + String((3 > 2) ? "molhado" : "seco"));
	display.display();
}

void mostrar_de_exposicao_ao_sol(int segundos) {
	for (int i = segundos; i > 0; i--) {
		display.clearDisplay();
		display.setCursor(0, 10);
		display.println("Banho de sol");
		display.setCursor(0, 20);
		display.println("por " + String(i));
		display.setCursor(0, 30);
		display.println("segundos");
		display.display();
		delay(1000); /* 1 segundo */
	}
}

void mostrar_aviso(String aviso) {
	display.clearDisplay();
	display.setCursor(0, 10);
	display.println(aviso);
	display.display();
}

void loop() {
	int umidade_do_solo = analogRead(A0);
	float temperatura = dht.readTemperature();
	while (isnan(temperatura)) {
		mostrar_aviso("Lendo temperatura...");
		delay(100);
		temperatura = dht.readTemperature();
	}

	if (sinal_de_acionamento_do_temporizador) {
		mostrar_de_exposicao_ao_sol(segundos_ao_sol);
		mostrar_aviso(avisos[aviso_fim_de_exposicao_ao_sol]);
		/* TODO: enviar aviso do fim de exposição ao sol. */
		delay(2000);
	}

	mostrar_temperatura_e_umidade(temperatura, umidade_do_solo);
	delay(2000); /* TODO: essa informação será atualizada a cada 5 minutos. */

	/* Será lançado se o solo nunca ficar úmido, mesmo quando
	   está sendo regado. */
	mostrar_aviso(avisos[aviso_reservatorio_vazio]);
	delay(2000);

	if (temperatura > temperatura_maxima_recomendada) {
		mostrar_aviso(avisos[aviso_temperatura_alta]);
		delay(2000);
	}

	if (temperatura < temperatura_minima_recomendada) {
		mostrar_aviso(avisos[aviso_temperatura_baixa]);
		delay(2000);
	}
}