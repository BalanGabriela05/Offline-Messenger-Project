#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <bits/pthreadtypes.h>
#include <sys/select.h>
#include <pthread.h>

/* codul de eroare returnat de anumite apeluri */
extern int errno;

/* portul de conectare la server*/
int port;

#define PORT 2555

void *receiveData(void *arg);
char mesaj[200];

typedef struct Logare
{
  char raspuns[100];
  char username[100];
  char password[100];
  char chosenUsername[100];
  char command;
  int isAuthenticated;
} Logare;

typedef struct History
{
  char comanda_h;
  char user_h[100];
  char raspuns_h[100];
} History;

typedef struct Reply
{
  char comanda_r;
  char id_r[100];
  char raspuns_r[100];
  char mesaj_r[100];

} Reply;

int main(int argc, char *argv[])
{
  int sd;                    // descriptorul de socket
  struct sockaddr_in server; // structura folosita pentru conectare
  int clientSocket;
  pthread_t receiveThread;

  if ((clientSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("Eroare la crearea socket-ului.");
    exit(EXIT_FAILURE);
  }

  memset(&server, 0, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_port = htons(PORT);
  server.sin_addr.s_addr = htonl(INADDR_ANY);

  if (connect(clientSocket, (struct sockaddr *)&server, sizeof(server)) == -1)
  {
    perror("Eroare la conectare.");
    exit(EXIT_FAILURE);
  }
  int parolagresita = 0;
  int isCommandValid = 0;
  int Choose = 0;
  int istoric = 0;
  int reply = 0;

  char scrieComanda[100]; // Variabila temporara pentru a citi comanda
  Logare logL;
  memset(&logL, 0, sizeof(Logare));

  while (!logL.isAuthenticated)
  {
    if (parolagresita == 0)
    {
      isCommandValid = 0;
      while (!isCommandValid)
      {

        printf("Inregistrare (r) sau Autentificare (l): ");
        fgets(scrieComanda, sizeof(scrieComanda), stdin);
        logL.command = scrieComanda[0];

        if (logL.command == 'r' || logL.command == 'l')
        {
          isCommandValid = 1;
        }
        else
        {
          printf("Comanda invalida. Te rog sa introduci 'r' sau 'l'.\n");
        }
      }

      printf("Username: ");
      fgets(logL.username, sizeof(logL.username), stdin);
      strtok(logL.username, "\n");
    }
    if (parolagresita <= 1)
    {
      printf("Password: ");
      fgets(logL.password, sizeof(logL.password), stdin);
      strtok(logL.password, "\n");
    }
    // Trimiterea datelor la server
    if (write(clientSocket, &logL, sizeof(Logare)) <= 0)
    {
      perror("[client]Eroare la write() catre server.\n");
    }

    // Așteptăm răspunsul de la server
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(clientSocket, &read_fds);

    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    int select_status = select(clientSocket + 1, &read_fds, NULL, NULL, &timeout);
    if (select_status < 0)
    {
      perror("[client]Eroare la select().\n");
      continue;
    }
    else if (select_status == 0)
    {
      printf("[client] Timeout la select().\n");
      continue;
    }

    if (FD_ISSET(clientSocket, &read_fds))
    {
      if (read(clientSocket, &logL.raspuns, sizeof(logL.raspuns)) <= 0)
      {
        perror("[client]Eroare la read() de la server.\n");
        continue;
      }

      printf("%s\n", logL.raspuns);
      if (strcmp(logL.raspuns, "Login successful") == 0 || strcmp(logL.raspuns, "User registered successfully") == 0)
      {
        logL.isAuthenticated = 1;
      }
      else if (strcmp(logL.raspuns, "Parola greșită") == 0)
      {
        memset(logL.password, 0, sizeof(logL.password));
        parolagresita = 1;
      }
    }
  }
  if (logL.isAuthenticated)
  {
    int count = 0;
    if (read(clientSocket, &count, sizeof(count)) <= 0)
    {
      perror("Eroare la read de la server.");
    }

    printf("Ai %d mesaje necitite\n", count);
    if (count > 0)
    {
      for (int i = 0; i < count; i++)
      {
        char unreadMessageBuffer[1024] = {0};
        ssize_t bytesRead = read(clientSocket, unreadMessageBuffer, sizeof(unreadMessageBuffer));
        if (bytesRead <= 0)
        {
          perror("Eroare la citirea mesajului necitit de la server.");
          // Gestionare eroare
          break; // Ieșire din buclă în caz de eroare
        }
        unreadMessageBuffer[bytesRead] = '\0'; 
        printf("%s", unreadMessageBuffer);     // Afișează mesajul necitit
      }
    }
    if (count > 0)
    {
      while (!reply)
      {
        char scrieComanda_r[10];
        Reply reL;
        memset(&reL, 0, sizeof(Reply));

        printf("Doriți să dați reply la un mesaj necitit? (y/n): ");
        fgets(scrieComanda_r, sizeof(scrieComanda_r), stdin);
        reL.comanda_r = scrieComanda_r[0];
        if (reL.comanda_r == 'y' || reL.comanda_r == 'Y')
        {
          printf("Introduceți ID-ul mesajului la care doriți să dați reply: ");
          fgets(reL.id_r, sizeof(reL.id_r), stdin);
          strtok(reL.id_r, "\n"); 
          printf("Introduceți mesajul: ");
          fgets(reL.mesaj_r, sizeof(reL.mesaj_r), stdin);
          strtok(reL.mesaj_r, "\n"); 

          // Trimiterea username-ului ales la server
          write(clientSocket, &reL, sizeof(Reply));

          // Așteaptă răspunsul de la server
          if (read(clientSocket, reL.raspuns_r, sizeof(reL.raspuns_r)) <= 0)
          {
            perror("Eroare la read de la server.");
          }

          printf("%s\n", reL.raspuns_r);
          if (strcmp(reL.raspuns_r, "Reply transmis") == 0)
          {
            reply = 0;
          }
        }
        else if (reL.comanda_r == 'n' || reL.comanda_r == 'N')
        {
          write(clientSocket, &reL, sizeof(Reply));
          // Așteaptă răspunsul de la server
          if (read(clientSocket, reL.raspuns_r, sizeof(reL.raspuns_r)) <= 0)
          {
            perror("Eroare la read de la server.");
          }
          printf("%s\n", reL.raspuns_r);

          if (strcmp(reL.raspuns_r, "Nu reply") == 0)
          {
            reply = 1;
          }
        }
        else
          printf("Comanda reply gresita.\n");
      }
    }
    while (!istoric)
    {
      char scrieComanda_h[10];
      History hiL;
      memset(&hiL, 0, sizeof(History));

      printf("Doriți să vedeți istoricul de mesaje cu un anumit utilizator? (y/n): ");
      fgets(scrieComanda_h, sizeof(scrieComanda_h), stdin);
      hiL.comanda_h = scrieComanda_h[0];
      if (hiL.comanda_h == 'y' || hiL.comanda_h == 'Y')
      {
        printf("Introduceți numele de utilizator pentru istoric: ");
        fgets(hiL.user_h, sizeof(hiL.user_h), stdin);
        strtok(hiL.user_h, "\n");

        // Trimiterea username-ului ales la server
        write(clientSocket, &hiL, sizeof(History));

        int count_h = 0;
        if (read(clientSocket, &count_h, sizeof(count_h)) <= 0)
        {
          perror("Eroare la read de la server.");
        }

        printf("Ai %d mesaje in istoric\n", count_h);
        if (count_h > 0)
        {
          for (int i = 0; i < count_h; i++)
          {
            char historyMessageBuffer[1024] = {0};
            ssize_t bytesRead = read(clientSocket, historyMessageBuffer, sizeof(historyMessageBuffer));
            if (bytesRead <= 0)
            {
              perror("Eroare la citirea mesajului necitit de la server.");
              
              
            }
            historyMessageBuffer[bytesRead] = '\0'; 
            printf("%s", historyMessageBuffer);     // Afișează mesajul necitit
          }
        }

        // Așteaptă răspunsul de la server
        if (read(clientSocket, hiL.raspuns_h, sizeof(hiL.raspuns_h)) <= 0)
        {
          perror("Eroare la read de la server.");
        }

        printf("%s\n", hiL.raspuns_h);
        if (strcmp(hiL.raspuns_h, "Istoric valid") == 0)
        {
          istoric = 1;
        }
      }
      else if (hiL.comanda_h == 'n' || hiL.comanda_h == 'N')
      {
        // Trimiterea username-ului ales la server
        write(clientSocket, &hiL, sizeof(History));

        // Așteaptă răspunsul de la server
        if (read(clientSocket, hiL.raspuns_h, sizeof(hiL.raspuns_h)) <= 0)
        {
          perror("Eroare la read de la server.");
        }
        printf("%s\n", hiL.raspuns_h);

        if (strcmp(hiL.raspuns_h, "Nu istoric") == 0)
        {
          istoric = 1;
        }
      }
      else
        printf("Coamnda istoric gresita.\n");
    }

    while (!Choose)
    {
      memset(logL.chosenUsername, 0, sizeof(logL.chosenUsername));
      memset(logL.raspuns, 0, sizeof(logL.raspuns));

      printf("Alege cu cine sa comunici (username): ");
      fgets(logL.chosenUsername, sizeof(logL.chosenUsername), stdin);
      strtok(logL.chosenUsername, "\n"); 

      // Trimiterea username-ului ales la server
      write(clientSocket, logL.chosenUsername, strlen(logL.chosenUsername) + 1);

      // Așteaptă răspunsul de la server
      if (read(clientSocket, logL.raspuns, sizeof(logL.raspuns)) <= 0)
      {
        perror("Eroare la read de la server.");
      }

      printf("%s\n", logL.raspuns);
      if (strcmp(logL.raspuns, "Utilizator valid") == 0)
      {
        Choose = 1;
      }
    }
  }

  if (Choose)
  {
    if (pthread_create(&receiveThread, NULL, receiveData, (void *)&clientSocket) != 0)
    {
      perror("Eroare la crearea thread-ului.");
      exit(EXIT_FAILURE);
    }

    printf("Introduceti :\n ");
    while (1)
    {

      fgets(mesaj, sizeof(mesaj), stdin);

      if (write(clientSocket, &mesaj, strlen(mesaj) + 1) == -1)
      {
        perror("Eroare la write catre server.");
        break;
      }
    }
  }
  pthread_join(receiveThread, NULL);
  close(clientSocket);

  return 0;
}
void *receiveData(void *arg)
{
  int clientSocket = *((int *)arg);
  char buffer[1024];

  while (1)
  {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(clientSocket, &read_fds);

    if (select(clientSocket + 1, &read_fds, NULL, NULL, NULL) < 0)
    {
      perror("Eroare la select().");
      break;
    }
    // Verificăm dacă există date pe socket-ul clientului

    if (FD_ISSET(clientSocket, &read_fds))
    {
      ssize_t bytesRead = read(clientSocket, buffer, sizeof(buffer) - 1);
      if (bytesRead <= 0)
      {
        if (bytesRead == 0)
        {
          // Serverul a închis conexiunea
          printf("Serverul a închis conexiunea\n");
        }
        else
        {
          perror("Eroare la read de la server.");
        }
        break;
      }
      buffer[bytesRead] = '\0';       printf("%s", buffer);
    }
  }

  pthread_exit(NULL);
}
