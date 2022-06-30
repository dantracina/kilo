#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>


struct termios orig_termios; /* Variável que guardará os atributos originais do terminal */

void disableRawMode() {
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}
void enableRawMode() {
	tcgetattr(STDIN_FILENO, &orig_termios);
	atexit(disableRawMode); /* Função interessante do stlib.h, "NA SAIDA", pois quebra implicitamente o fluxo do programa */
	
	struct termios raw = orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	/*
	 * IXON - desabilita os sinais do CtrlS (impede os dados para o terminal) e CtrlQ (desfaz o CtrlS)
	 * ICRNL - desabilita o CrtlM e CtrlJ, tendo em vista que Cr - carriage return 'r' e Nl - '\n'
	 * Os demais sinalizadores não possuem um efeito observável
	*/
	raw.c_oflag &= ~(OPOST);
	/*
	 * OPOST - desativa a saída padrão do new line que é '\r\n' para apenas '\n'
	*/
	
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | ISIG); 
	/*
	 * ECHO - impressão de teclas pressionadas na tela,
	 * ICANON - desliga a leitura de frase por frase, e, lemos por caracter
	 * ISIG - Desliga os sinais SIGINT do CtrlD e SIGSTP do CtrlZ
	*/ 
	
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
	enableRawMode();
	
	char c;
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') { /* Lê um 1 byte por vez até o final e retorna o número de bytes lidos */
		if(iscntrl(c)) {
			printf("%d\r\n", c);
		} else {
			printf("%d ('%c')\r\n", c, c);
		}
	}

	return 0;
}
