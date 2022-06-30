/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/
struct termios orig_termios; /* Variável que guardará os atributos originais do terminal */

/*** terminal ***/
void die(const char *s) {
	perror(s); /* A maioria das fç do std que falham definirá uma varíavel global errno, perror avalia essa variável e imprime uma msg para ela */
	exit(1);
	/*
	 * perror - stdio.h
	 * exit - stdlib.h
	*/
}
void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
		die("tcsetattr");
}
void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
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
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	/*
	 * são index do control characters
	 * O VMIN define o número mínimo de bytes de entrada necessários antes que o read possa retornar
	 * Neste caso, o 0 significa assim que algum byte seja lido
	 * O VTIME define o tempo máximo para que o read() dê retorno, neste caso 0.1 segundos
	*/
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

char editorReadKey() {
	int nread;
	char c;
	while ( (nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if(nread == -1 && errno != EAGAIN) die("read");
	}
	return c;	
}

/*** input ***/

void editorProcessKeypress() {
	char c = editorReadKey();
	
	switch(c) {
		case CTRL_KEY('q'):
			exit(0);
			break;
	}
}
/*** init ***/
int main() {
	enableRawMode();
	
	while(1) {
		editorProcessKeypress();
	}
	
	return 0;
}
