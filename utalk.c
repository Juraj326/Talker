#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>

static volatile int stop = 0; // ak stop != 0 tak sa zastavi cyklus s posielanim a prijimanim sprav

// odchytava signal SIGINT (ctrl+c)
void signalHandler(int signal)
{
  write(STDIN_FILENO, "\nOdchytil som ^C\n", 17);
  stop = signal;
}

// perror a exit zluceny
void perrorExit(char *errorMessage)
{
  perror(errorMessage);
  exit(1);
}

// hlasenia chyb spojene s IO
void ioErrorExit(char *errorMessage)
{
  printf("%s\n", errorMessage);
  exit(1);
}

// maze aktualny riadok v terminali
void deleteLine(int i)
{
  for (int c = 0; c < i; c++)
    write(STDOUT_FILENO, "\b \b", 3);
}

int main(int argc, char const *argv[])
{
  signal(SIGINT, signalHandler);

  if (argc < 2 || argc > 3)
    ioErrorExit("Nespravny pocet argumentov. Zadajte argumenty v tvare [ipv4 adresa] [cislo portu]");

  // default port 6969, ak sa udava medzi argumentami port, tak ho nahradi portom z argumentu
  int port = 6969;
  if (argc == 3)
  {
    int i = 0;
    while (1)
    {
      if (argv[2][i] == '\0')
        break;

      if (isdigit(argv[2][i]) == 0)
        ioErrorExit("Druhy argument nie je cislom portu");

      i++;
    }
    port = atoi(argv[2]);
    if (port < 1024)
      ioErrorExit("Privilegovany port. Pouzite port > 1023");
  }

  // lokalna adresa
  struct sockaddr_in host;
  host.sin_family = AF_INET;
  host.sin_port = htons(port);
  host.sin_addr.s_addr = INADDR_ANY;

  // adresa, na ktoru sa bude spajat
  struct sockaddr_in client;
  if (inet_pton(AF_INET, argv[1], &client.sin_addr) != 1)
    ioErrorExit("Nekompatibilna adresa. Kompatibilna adresa je v tvare ddd.ddd.ddd.ddd");

  client.sin_family = AF_INET;
  client.sin_port = htons(port);
  uint sizeofClient = sizeof(client);

  // vytvorenie socketu pre UDP
  int socketFD = socket(AF_INET, SOCK_DGRAM, 0);
  if (socketFD == -1)
    perrorExit("Zlyhal socket(). Vnutorna chyba");

  // nastavenie lokalnej adresy
  if (bind(socketFD, (struct sockaddr *)&host, sizeof(host)) == -1)
    perrorExit("Zlyhal bind()");

  // nastavenie vzdialenej adresy
  if (connect(socketFD, (struct sockaddr *)&client, sizeofClient) == -1)
    perrorExit("Zlyhal connect()");

  printf("Zaciatok konverzacie s %s na porte %d\n", inet_ntoa(client.sin_addr), port);

  // Ziskanie udajov terminalu
  struct termios termattr;
  if (tcgetattr(STDIN_FILENO, &termattr) == -1)
    perrorExit("Zlyhal tcgetattr(). Vnutorna chyba");

  fd_set readFD; // mnozina file descriptorov

  int receiving = 0; // 1 ak druhy pise
  int typing = 0;    // 1 ak ja pisem
  int i = 0;         // pozicia v hostMessage
  char in;           // odchytavanie pisania druheho
  char out;          // char, ktory nacital zo stdin do bufferu hostMessage

  char *hostMessage = calloc(101, 1);   // buffer pre spravu, ktoru bude odosielat
  char *clientMessage = calloc(101, 1); // buffer pre spravu, ktoru bude prijimat
  const int maxMessageLength = 101;     // maximalna dlzka msg je 100 + ukoncovacia 0

  // moja implementacia vyzaduje nekanonicky mod
  termattr.c_lflag &= ~ICANON;
  if (tcsetattr(STDIN_FILENO, TCSANOW, &termattr) == -1)
    perrorExit("Zlyhal tcsetattr()(ICANON OFF). Vnutorna chyba");

  while (!stop)
  {
    // resetuje mnoziny pre select
    FD_ZERO(&readFD);
    FD_SET(STDIN_FILENO, &readFD);
    FD_SET(socketFD, &readFD);

    if (select(socketFD + 1, &readFD, NULL, NULL, NULL) == -1)
    {
      if (errno == EINTR) // ked dojde k preruseniu systemoveho volania pri odchytavani ctrl+c
        continue;
      else
        perrorExit("Zlyhal select(). Vnutorna chyba");
    }

    // prijimanie sprav
    if (FD_ISSET(socketFD, &readFD))
    {
      if (receiving == 0)
      {
        // odchytenie pisania
        if (recv(socketFD, &in, 1, 0) == -1)
          perrorExit("\nPrijimanie spravy zlyhalo. Druha strana pravdepodobne ukoncila chod programu. Vnutorna chyba");

        if (typing == 1) // ak pisem aj ja tak moju spravu chcem vidiet pod oznamom
          deleteLine(i); // zmaze riadok s mojou spravou ak druhy pise

        printf("%s zacal pisat\n", inet_ntoa(client.sin_addr));
        receiving = 1; // druha strana pise -> receiving = 1

        if (typing == 1)                                          // ak stale pisem tak chcem aby moja sprava bola pod prijatou spravou
          write(STDOUT_FILENO, hostMessage, strlen(hostMessage)); // posunie moj riadok so spravou pod oznam ze druhy pise
      }
      else
      {
        // zvysok spravy
        if (recv(socketFD, clientMessage, maxMessageLength, 0) == -1)
          perrorExit("\nPrijimanie spravy zlyhalo. Druha strana pravdepodobne ukoncila chod programu. Vnutorna chyba");

        if (typing == 1)
          deleteLine(i); // zmaze riadok s mojou spravou ak druhy pise

        printf("(%s): %s\n", inet_ntoa(client.sin_addr), clientMessage);

        if (typing == 1)
          write(STDOUT_FILENO, hostMessage, strlen(hostMessage)); // posunie moj riadok so spravou pod oznam ze druhy pise

        memset(clientMessage, 0, maxMessageLength); // resetuje buffer
        receiving = 0;                              // prijal som spravu druhej strany -> receiving = 0
      }
    }

    // odosielanie sprav
    else if (FD_ISSET(STDIN_FILENO, &readFD))
    {
      if (i > 99)
        ioErrorExit("Sprava presiahla limit 100 znakov.\n");

      if (typing == 0)
      {
        // odchytenie pisania
        out = getc(stdin);
        hostMessage[i] = out;                 // postupne pridava jednotlive znaky do buffera
        if (send(socketFD, &out, 1, 0) == -1) // posle prvy znak, sluzi na oznamenie druhej strane, ze pisem
          perrorExit("\nOdosielanie spravy zlyhalo. Vnutorna chyba");

        typing = 1; // zacal som pisat -> typing = 1
      }
      else
      {
        // zvysok spravy
        out = getc(stdin);
        if (out == 127) // delete control character
        {
          // odstrani znak z terminalu, u mna pri backspace vypisuje na terminal ^?, vkoli tomu tam je viac \b
          write(STDOUT_FILENO, "\b \b\b \b\b \b", 9); // vymazanie 1 znaku = \b \b, 3
          i -= 2;
          hostMessage[i + 1] = '\0'; // odstrani znak z buffera
        }
        else if (out == '\n') // enter
        {
          hostMessage[i] = '\0'; // ukonci spravu v bufferi
          if (send(socketFD, hostMessage, strlen(hostMessage), 0) == -1)
            perrorExit("\nOdosielanie spravy zlyhalo. Druha strana pravdepodobne ukoncila chod programu");

          memset(hostMessage, 0, maxMessageLength); // resetne buffer pre spravy
          typing = 0;                               // odosielam spravu celu spravu -> typing = 0
          i = -1;                                   // resetuje poziciu v hostMessage, -1 lebo sa hned inkrementuje na 0
        }
        else
          hostMessage[i] = out; // zapise znak do buffera
      }
      i++;
    }
  }

  free(hostMessage);
  free(clientMessage);
  close(socketFD);
  printf("Utalk bol ukonceny.\n");
  return 0;
}