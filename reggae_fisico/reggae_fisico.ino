/*
 O seguintes dados virão por MQTT:
  sinal_de_acionamento_do_temporizador; e
	segundos_ao_sol.
*/

#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define LIMIAR_DA_UMIDADE_DO_SOLO 530

#define PINO_DO_DHT D4
#define TIPO_DO_DHT DHT11

#define LARGURA_DA_TELA 128
#define ALTURA_DA_TELA 64
#define ENDERECO_I2C_DO_OLED 0x3C

DHT dht(PINO_DO_DHT, TIPO_DO_DHT);

Adafruit_SH1106G display(LARGURA_DA_TELA, ALTURA_DA_TELA, &Wire, -1);

bool sinal_de_acionamento_do_temporizador = true; /* valor temporário */
int segundos_ao_sol = 3; /* valor temporário */

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
	String molhado_ou_seco = "seco";

	display.clearDisplay();
	display.setCursor(0, 10);
	display.println("Temperatura: " + String(temperatura) + " C");
	display.setCursor(0, 20);
	if (umidade > LIMIAR_DA_UMIDADE_DO_SOLO) {
		molhado_ou_seco = "seco";
	} else {
		molhado_ou_seco = "molhado";
	}
	display.println("Umidade: " + String(umidade) + " %");
	display.setCursor(0, 30);
	display.println(molhado_ou_seco + ".");
	display.display();
}
/* Falta implementar o envio com o MQTT */
void enviar_aviso(String motivo_do_aviso) {
	display.clearDisplay();
	display.setCursor(0, 10);
	display.println("Aviso disparado!");
	display.setCursor(0, 20);
	display.println(motivo_do_aviso);
	display.display();
}

void mostrar_comeco_do_tempo_ao_sol(int segundos) {
	for (int i = segundos; i > 0; i--) {
		display.clearDisplay();
		display.setCursor(0, 10);
		display.println("Comeco de exposicao");
		display.setCursor(0, 20);
		display.println("por " + String(i));
		display.setCursor(0, 30);
		display.println("segundos");
		display.display();
		delay(segundos_para_milissegundos(i));
	}
}

void mostrar_fim_do_tempo_ao_sol() {
	display.clearDisplay();
	display.setCursor(0, 10);
	display.println("Fim de exposicao");
	display.setCursor(0, 20);
	display.println("ao sol");
	display.display();
}

void loop() {
	int umidade_do_solo = analogRead(A0);
	float temperatura = dht.readTemperature();

	if (sinal_de_acionamento_do_temporizador) {
		mostrar_comeco_do_tempo_ao_sol(segundos_ao_sol);
		mostrar_fim_do_tempo_ao_sol();
		delay(2000);
		enviar_aviso("Fim do banho de sol");
		delay(2000);
	}

	mostrar_temperatura_e_umidade(temperatura, umidade_do_solo);
	delay(2000);
}