#include <unistd.h>

int main() {
	char c;
	while (read(STDIN_FILENO, &c, 1) == 1); /* Lê um 1 byte por vez, até o final e retorna o número de bytes lidos */
	return 0;
}
