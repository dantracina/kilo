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
	raw.c_lflag &= ~(ECHO | ICANON); 
	
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
	enableRawMode();
	
	char c;
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q'); /* Lê um 1 byte por vez até o final e retorna o número de bytes lidos */
	
	return 0;
}
