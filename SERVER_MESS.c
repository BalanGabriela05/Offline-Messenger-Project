#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <stdint.h>
#include <asm-generic/socket.h>
#include <sys/select.h>
#include <sqlite3.h>
/* portul folosit */
#define PORT 2555

/* codul de eroare returnat de anumite apeluri */
extern int errno;

typedef struct thData
{
  int idThread; // id-ul thread-ului tinut in evidenta de acest program
  int cl;       // descriptorul intors de accept
} thData;
typedef struct ClientInfo
{
  int clienti[4];
  int count;
  char usernames[4][100];
  pthread_mutex_t mutex; // mutex pentru sincronizarea accesului la listă
} ClientInfo;

ClientInfo lista_clienti;

typedef struct Logare
{
  char raspuns[100];
  char username[100];
  char password[100];
  char chosenUsername[100];
  char command;
  int isAuthenticated;
} Logare;

typedef struct Reply
{
  char comanda_r;
  char id_r[100];
  char raspuns_r[100];
  char mesaj_r[100];

} Reply;

typedef struct History
{
  char comanda_h;
  char user_h[100];
  char raspuns_h[100];
} History;
typedef struct
{
  int id;
  char sender[100];
  char message[256];
  char timestamp[100]; 
} HistoryMessage;

typedef struct
{
  int id;
  char sender[100];
  char message[256];
  char timestamp[100];
} UnreadMessage;

char mesaj[200];
static void *treat(void *);
void raspunde(thData *, Logare *, History *, Reply *);
void to_specific_client(char *message, int id_curent, char *targetUsername, int replyTo);
const char *USER_FILE = "users.txt";
void registerUser(char *, char *);
int loginUser(char *, char *);
int checkIfUserExists(char *);
void saveMessage(const char *sender, const char *receiver, const char *message, int read, int replyTo);
void MessageHistory(const char *user1, const char *user2, int clientSocket);
void UnreadMessages(sqlite3 *db, const char *receiver, int clientSocket);
void updateMessageStatusAsRead(const char *receiver);
void processMessageReply(thData *tdL, const char *sender, int messageId, const char *replyMessage);
static int replyCallback(void *data, int argc, char **argv, char **azColName);
static int collectUnreadMessagesCallback(void *NotUsed, int argc, char **argv, char **azColName);
sqlite3 *db;
char *errMsg = 0;
int rc;

int count; // Numărător pentru mesaje necitite
int count_h;
UnreadMessage unreadMessages[100];
HistoryMessage historyMessages[100];
// Funcție pentru inițializarea bazei de date
void initDatabase()
{
  rc = sqlite3_open("chat_history.db", &db);

  if (rc)
  {
    fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
    return;
  }
  else
  {
    fprintf(stderr, "Opened database successfully\n");
  }

  // Creare tabel dacă nu există
  char *sql = "CREATE TABLE IF NOT EXISTS MESSAGES ("
              "ID INTEGER PRIMARY KEY AUTOINCREMENT,"
              "SENDER TEXT NOT NULL,"
              "RECEIVER TEXT NOT NULL,"
              "MESSAGE TEXT NOT NULL,"
              "READ INTEGER DEFAULT 0,"
              "REPLY_TO INTEGER DEFAULT NULL,"
              "TIMESTAMP DATETIME DEFAULT CURRENT_TIMESTAMP);";

  rc = sqlite3_exec(db, sql, 0, 0, &errMsg);

  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "SQL error: %s\n", errMsg);
    sqlite3_free(errMsg);
  }
  else
  {
    fprintf(stdout, "Table created successfully\n");
  }
}

int main()
{
  struct sockaddr_in server; // structura folosita de server
  struct sockaddr_in from;
  int sd; // descriptorul de socket
  int pid;
  pthread_t th[100]; // Identificatorii thread-urilor care se vor crea
  int i = 0;
  initDatabase();
  /* crearea unui socket */
  if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("[server]Eroare la socket().\n");
    return errno;
  }
  /* utilizarea optiunii SO_REUSEADDR */
  int on = 1;
  setsockopt(sd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &on, sizeof(on));

  /* pregatirea structurilor de date */
  bzero(&server, sizeof(server));
  bzero(&from, sizeof(from));

  /* umplem structura folosita de server */
  /* stabilirea familiei de socket-uri */
  server.sin_family = AF_INET;
  /* acceptam orice adresa */
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  /* utilizam un port utilizator */
  server.sin_port = htons(PORT);

  /* atasam socketul */
  if (bind(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
  {
    perror("[server]Eroare la bind().\n");
    return errno;
  }

  /* punem serverul sa asculte daca vin clienti sa se conecteze */
  if (listen(sd, 2) == -1)
  {
    perror("[server]Eroare la listen().\n");
    return errno;
  }
  /* servim in mod concurent clientii...folosind thread-uri */

  printf("[server]Asteptam la portul %d...\n", PORT);
  fflush(stdout);

  lista_clienti.count = 0;
  pthread_mutex_init(&lista_clienti.mutex, NULL);

  while (1)
  {
    int client;
    thData *td; // parametru functia executata de thread

    int length = sizeof(from);

    // client= malloc(sizeof(int));
    /* acceptam un client (stare blocanta pina la realizarea conexiunii) */
    if ((client = accept(sd, (struct sockaddr *)&from, &length)) < 0)
    {
      perror("[server]Eroare la accept().\n");
      continue;
    }

    /* s-a realizat conexiunea, se astepta mesajul */


    td = (struct thData *)malloc(sizeof(struct thData));
    td->idThread = i++;
    td->cl = client;

    if (pthread_create(&th[i], NULL, &treat, td))
    {
      perror("[server]Eroare la crearea thread-ului.\n");
      return errno;
    }

  } // while
  pthread_mutex_destroy(&lista_clienti.mutex);
  close(sd);
  sqlite3_close(db);
  return 0;
}

static void *treat(void *arg)
{
  struct thData tdL;
  tdL = *((struct thData *)arg);
  struct Logare logL;
  memset(&logL, 0, sizeof(Logare));
  struct History hiL;
  memset(&hiL, 0, sizeof(History));
  struct Reply reL;
  memset(&reL, 0, sizeof(Reply));
  printf("[thread]- %d - Asteptam mesajul...\n", tdL.idThread);
  fflush(stdout);
  pthread_detach(pthread_self());

  raspunde(&tdL, &logL, &hiL, &reL);

  close(tdL.cl);

  pthread_mutex_lock(&lista_clienti.mutex);
  for (int i = 0; i < lista_clienti.count; ++i)
  {
    if (lista_clienti.clienti[i] == tdL.cl)
    {
      // Eliminăm clientul din lista la deconectare
      for (int j = i; j < lista_clienti.count - 1; ++j)
      {
        lista_clienti.clienti[j] = lista_clienti.clienti[j + 1];
      }
      lista_clienti.count--;
      break;
    }
  }
  pthread_mutex_unlock(&lista_clienti.mutex);

  free(arg);

  pthread_exit(NULL);
}

void raspunde(thData *tdL, Logare *logL, History *hiL, Reply *reL)
{
  int i = 0;
  int Choose = 0;
  int istoric = 0;
  int reply = 0;

  while (!logL->isAuthenticated)
  {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(tdL->cl, &read_fds);

    struct timeval timeout;
    timeout.tv_sec = 5; // Timeout după 5 secunde
    timeout.tv_usec = 0;

    int activity = select(tdL->cl + 1, &read_fds, NULL, NULL, &timeout);
    if ((activity < 0) && (errno != EINTR))
    {
      perror("[server]Eroare la select().\n");
      break;
    }

    if (FD_ISSET(tdL->cl, &read_fds))
    {
      ssize_t read_bytes = read(tdL->cl, logL, sizeof(Logare));
      if (read_bytes <= 0)
      {
        perror("[server]Eroare la read() de la client.\n");
        break;
      }

      if (logL->command == 'r')
      {
        if (checkIfUserExists(logL->username))
        {
          printf("Username already exists. Try again.\n");
          strcpy(logL->raspuns, "Username already exists. Try again.");
        }
        else
        {
          registerUser(logL->username, logL->password);
          printf("User registered successfully\n");
          logL->isAuthenticated = 1;
          strcpy(logL->raspuns, "User registered successfully");
        }
      }

      if (logL->command == 'l')
      {
        int loginResult = loginUser(logL->username, logL->password);
        if (loginResult == 1)
        {
          printf("Login successful\n");
          logL->isAuthenticated = 1;
          strcpy(logL->raspuns, "Login successful");
        }
        else if (loginResult == -1)
        {
          printf("Parola greșită. Încearcă din nou.\n");
          strcpy(logL->raspuns, "Parola greșită");
        }
        else
        {
          printf("Login failed, try again.\n");
          strcpy(logL->raspuns, "Login failed, try again.");
        }
      }

      ssize_t write_bytes = write(tdL->cl, &logL->raspuns, sizeof(logL->raspuns));
      if (write_bytes <= 0)
      {
        perror("[server]Eroare la write() .\n");
      }
      else
      {
        printf("Răspuns trimis clientului %d.\n", tdL->idThread);
      }
    }
  }

  if (logL->isAuthenticated)
  {

    pthread_mutex_lock(&lista_clienti.mutex);
    lista_clienti.clienti[lista_clienti.count] = tdL->cl;
    strcpy(lista_clienti.usernames[lista_clienti.count], logL->username);
    lista_clienti.count++;
    pthread_mutex_unlock(&lista_clienti.mutex);

    //mesaje necitite
    UnreadMessages(db, logL->username, tdL->cl);
    updateMessageStatusAsRead(logL->username);
    
    if(count>0){
    
    // reply
    while (!reply)
    { // mesaje offline

      printf("REPLY\n");
      ssize_t read_bytes = read(tdL->cl, reL, sizeof(Reply));
      if (read_bytes <= 0)
      {
        perror("[server]Eroare la read() de la client.\n");
      }
     
      if (reL->comanda_r == 'y' || reL->comanda_r == 'Y')
      {

        printf("Reply transmis\n");
        strcpy(reL->raspuns_r, "Reply transmis");
        
        int messageId = atoi(reL->id_r);
        processMessageReply(tdL, logL->username, messageId, reL->mesaj_r);
      }
      else if (reL->comanda_r == 'n' || reL->comanda_r == 'N')
      {
        printf("Nu reply\n");
        strcpy(reL->raspuns_r, "Nu reply");
        reply = 1;
      }
      ssize_t write_bytes = write(tdL->cl, &reL->raspuns_r, sizeof(reL->raspuns_r));
      if (write_bytes <= 0)
      {
        perror("[server]Eroare la write() .\n");
      }
      else
      {
        printf("Răspuns trimis clientului %d.\n", tdL->idThread);
      }
    }
    }

    // istoric afisare
    while (!istoric)
    {
      printf("ISTORIC\n");
      ssize_t read_bytes = read(tdL->cl, hiL, sizeof(History));
      if (read_bytes <= 0)
      {
        perror("[server]Eroare la read() de la client.\n");
      }
      else
        printf("%c\n", hiL->comanda_h);
      if (hiL->comanda_h == 'n' || hiL->comanda_h == 'N')
      {
        printf("Nu istoric\n");
        strcpy(hiL->raspuns_h, "Nu istoric");
        istoric = 1;
      }
      else if (hiL->comanda_h == 'y' || hiL->comanda_h == 'Y')
      {
        MessageHistory(logL->username, hiL->user_h,tdL->cl);
        printf("Istoric valid\n");
        istoric = 1;
        strcpy(hiL->raspuns_h, "Istoric valid");
      }
      ssize_t write_bytes = write(tdL->cl, &hiL->raspuns_h, sizeof(hiL->raspuns_h));
      if (write_bytes <= 0)
      {
        perror("[server]Eroare la write() .\n");
      }
      else
      {
        printf("Răspuns trimis clientului %d.\n", tdL->idThread);
      }
    }

    // Comunicarea între clienți
    while (!Choose)
    {
      if (read(tdL->cl, logL->chosenUsername, sizeof(logL->chosenUsername)) <= 0)
      {
        perror("[server]Eroare la read() de la client.\n");
        break;
      }

      // Verificarea și trimiterea răspunsului
      if (checkIfUserExists(logL->chosenUsername))
      {
        printf("Utilizator valid\n");
        strcpy(logL->raspuns, "Utilizator valid");
        Choose = 1;
      }
      else
      {
        printf("Username-ul nu există. Te rog să reintroduci.\n");
        strcpy(logL->raspuns, "Username-ul nu există. Te rog să reintroduci.");
      }
      write(tdL->cl, logL->raspuns, strlen(logL->raspuns));
    }

    while (1)
    {
      fd_set read_fds;
      int fdmax = tdL->cl + 1;

      FD_ZERO(&read_fds);
      FD_SET(tdL->cl, &read_fds);

      struct timeval timeout;
      timeout.tv_sec = 1;
      timeout.tv_usec = 0;

      if (select(fdmax, &read_fds, NULL, NULL, &timeout) == -1)
      {
        perror("[server]Eroare la select().\n");
        return;
      }

      if (FD_ISSET(tdL->cl, &read_fds))
      {
        if (read(tdL->cl, &mesaj, sizeof(mesaj)) <= 0)
        {
          printf("[Thread %d]\n", tdL->idThread);
          perror("Eroare la read() de la client.\n");
          break;
        }

        printf("[Thread %d] Mesajul a fost receptionat...%s\n", tdL->idThread, mesaj);

        printf("[Thread %d] Trimitem mesajul inapoi...%s\n", tdL->idThread, mesaj);

        // Trimiteți mesajul către toți ceilalți clienți
        printf("[Client %d] a spus: %s\n", tdL->idThread, mesaj);
        to_specific_client(mesaj, tdL->cl, logL->chosenUsername, 1);
      }
    }
  }
}
void to_specific_client(char *message, int id_curent, char *targetUsername, int replyTo)
{
  pthread_mutex_lock(&lista_clienti.mutex);
  char *senderUsername = NULL;
  int isRecipientConnected = 0;
  int recipientClientId = -1;

  // Verifică dacă expeditorul este conectat și găsește numele de utilizator al expeditorului
  for (int i = 0; i < lista_clienti.count; ++i)
  {
    if (lista_clienti.clienti[i] == id_curent)
    {
      senderUsername = lista_clienti.usernames[i];
      break;
    }
  }

  // Verifică dacă destinatarul este conectat
  for (int i = 0; i < lista_clienti.count; ++i)
  {
    if (strcmp(lista_clienti.usernames[i], targetUsername) == 0)
    {
      isRecipientConnected = 1;
      recipientClientId = lista_clienti.clienti[i];
      break;
    }
  }

  // Dacă destinatarul este conectat, trimite mesajul și marchează-l ca citit
  if (isRecipientConnected)
  {
    char finalMessage[256];
    sprintf(finalMessage, "[Client %s] %s", senderUsername, message);
    if (write(recipientClientId, finalMessage, strlen(finalMessage) + 1) <= 0)
    {
      perror("[server]Eroare la write() catre client.\n");
    }
    saveMessage(senderUsername, targetUsername, message, 1, replyTo); // 1 pentru mesaj citit
  }
  else
  {
    // Dacă destinatarul nu este conectat, salvează mesajul ca necitit
    saveMessage(senderUsername, targetUsername, message, 0, replyTo); // 0 pentru mesaj necitit
  }

  pthread_mutex_unlock(&lista_clienti.mutex);
}

void registerUser(char *username, char *password)
{
  FILE *file = fopen(USER_FILE, "a");
  if (file == NULL)
  {
    perror("Error opening user file.");
    return;
  }
  fprintf(file, "%s %s\n", username, password);
  printf("AM INREGISTRAT\n");
  fclose(file);
}
int loginUser(char *username, char *password)
{
  FILE *file = fopen(USER_FILE, "r");
  char fileUsername[100], filePassword[100];
  int userFound = 0;

  if (file == NULL)
  {
    perror("Error opening user file.");
    return 0; // Nu s-a putut deschide fișierul
  }

  while (fscanf(file, "%s %s", fileUsername, filePassword) != EOF)
  {
    if (strcmp(username, fileUsername) == 0)
    {
      userFound = 1;
      if (strcmp(password, filePassword) == 0)
      {
        fclose(file);
        return 1; 
      }
      break; 
    }
  }

  fclose(file);
  if (userFound)
  {
    return -1;
  }
  return 0;
}
int checkIfUserExists(char *username)
{
  FILE *file = fopen(USER_FILE, "r");
  char fileUsername[100];
  if (file == NULL)
  {
    perror("Error opening user file.");
    return 0;
  }
  while (fscanf(file, "%s", fileUsername) != EOF)
  {
    if (strcmp(username, fileUsername) == 0)
    {
      fclose(file);
      printf("EXISTA USER\n");
      return 1; 
    }
    // Skip password
    fscanf(file, "%s", fileUsername);
  }
  fclose(file);
  return 0; 
}

void saveMessage(const char *sender, const char *receiver, const char *message, int read, int replyTo)
{
  char sql[512];
  sprintf(sql, "INSERT INTO MESSAGES (SENDER, RECEIVER, MESSAGE, READ, REPLY_TO) VALUES ('%s', '%s', '%s', %d, %d);",
          sender, receiver, message, read, replyTo);

  rc = sqlite3_exec(db, sql, 0, 0, &errMsg);
  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "SQL error: %s\n", errMsg);
    sqlite3_free(errMsg);
  }
  else
  {
    fprintf(stdout, "Message saved successfully\n");
  }
}

// Callback pentru a procesa fiecare rând returnat de interogare

static int collectHistoryMessagesCallback(void *NotUsed, int argc, char **argv, char **azColName)
{
  if (count_h < 100)
  {
    historyMessages[count_h].id = atoi(argv[0]);
    strncpy(historyMessages[count_h].sender, argv[1], sizeof(historyMessages[count_h].sender));
    strncpy(historyMessages[count_h].message, argv[3], sizeof(historyMessages[count_h].message));
    strncpy(historyMessages[count_h].timestamp, argv[6], sizeof(historyMessages[count_h].timestamp));
    count_h++;
  }
  return 0;
}

void MessageHistory(const char *user1, const char *user2, int clientSocket)
{
  char sql[512];
  sprintf(sql, "SELECT * FROM MESSAGES WHERE (SENDER='%s' AND RECEIVER='%s') OR (SENDER='%s' AND RECEIVER='%s') ORDER BY TIMESTAMP;", user1, user2, user2, user1);

  // Resetăm numărătorul de istoric
  count_h = 0;

  char *errMsg = 0;
  int rc = sqlite3_exec(db, sql, collectHistoryMessagesCallback, 0, &errMsg);
  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "SQL error: %s\n", errMsg);
    sqlite3_free(errMsg);
  }
  else
  {
    fprintf(stdout, "History messages collected successfully\n");
  }
  printf("Exista %d mesaje in istoric\n", count_h);
 
  // Trimite numărul de mesaje istorice la client
  write(clientSocket, &count_h, sizeof(count_h));

  // Trimite fiecare mesaj istoric la client
  for (int i = 0; i < count_h; i++)
  {
    char buffer2[1024] = {0};
    sprintf(buffer2, "[id:%d] %s [%s]: %s\n",
            historyMessages[i].id,
            historyMessages[i].sender,
            historyMessages[i].timestamp,
            historyMessages[i].message);
    write(clientSocket, buffer2, sizeof(buffer2));
  }
}

// Callback pentru colectarea mesajelor necitite
static int collectUnreadMessagesCallback(void *NotUsed, int argc, char **argv, char **azColName)
{
  if (count < 100)
  {
    unreadMessages[count].id = atoi(argv[0]);
    strncpy(unreadMessages[count].sender, argv[1], sizeof(unreadMessages[count].sender));
    strncpy(unreadMessages[count].message, argv[3], sizeof(unreadMessages[count].message));
    strncpy(unreadMessages[count].timestamp, argv[6], sizeof(unreadMessages[count].timestamp));
    count++;
  }
  return 0;
}

// Funcția pentru interogarea mesajelor necitite
void UnreadMessages(sqlite3 *db, const char *receiver, int clientSocket)
{
  char sql[512];
  char *errMsg = 0;
  int rc;

  // Resetăm numărătorul de mesaje necitite
  count = 0;

  sprintf(sql, "SELECT * FROM MESSAGES WHERE RECEIVER='%s' AND READ=0 ORDER BY TIMESTAMP;", receiver);
  rc = sqlite3_exec(db, sql, collectUnreadMessagesCallback, 0, &errMsg);

  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "SQL error: %s\n", errMsg);
    sqlite3_free(errMsg);
  }
  else
  {
    fprintf(stdout, "Unread messages collected successfully\n");
  }
printf("Exista %d mesaje necitite\n",count);
write(clientSocket, &count, sizeof(count));

 if (count > 0) {
        for (int i = 0; i < count; i++) {
            char buffer[1024]={0};
            sprintf(buffer, "Mesaj necitit de la %s [id:%d][%s]: %s\n", 
                    unreadMessages[i].sender, 
                    unreadMessages[i].id,
                    unreadMessages[i].timestamp, 
                    unreadMessages[i].message);
            write(clientSocket, buffer, sizeof(buffer));
        }
    }
}

// Funcția pentru a actualiza starea mesajelor ca fiind citite
void updateMessageStatusAsRead(const char *receiver)
{
  char sql[512];
  sprintf(sql, "UPDATE MESSAGES SET READ=1 WHERE RECEIVER='%s' AND READ=0;", receiver);

  rc = sqlite3_exec(db, sql, 0, 0, &errMsg);
  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "SQL error: %s\n", errMsg);
    sqlite3_free(errMsg);
  }
  else
  {
    fprintf(stdout, "Messages updated as read successfully\n");
  }
}
// Declarăm funcția callback în afara funcției processMessageReply
// Redenumiți funcția callback pentru interogarea specifică din processMessageReply
static int replyCallback(void *data, int argc, char **argv, char **azColName)
{
  char **originalInfo = (char **)data;
  strcpy(originalInfo[0], argv[0]); // originalSender
  strcpy(originalInfo[1], argv[1]); // originalReceiver
  return 0;
}

void processMessageReply(thData *tdL, const char *sender, int messageId, const char *replyMessage)
{
  char sql[512];
  sprintf(sql, "SELECT SENDER, RECEIVER FROM MESSAGES WHERE ID=%d", messageId);

  char originalSender[100];
  char originalReceiver[100];
  char *originalInfo[2] = {originalSender, originalReceiver};
  int found = 0;

  rc = sqlite3_exec(db, sql, replyCallback, originalInfo, &errMsg);
  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "SQL error: %s\n", errMsg);
    sqlite3_free(errMsg);
    return;
  }

  // Verificăm dacă avem un rezultat (dacă originalSender și originalReceiver au fost setate)
  if (originalSender[0] != '\0' && originalReceiver[0] != '\0')
  {
    found = 1;
  }

  if (found)
  {
    saveMessage(sender, originalSender, replyMessage, 0, messageId);
  }
}

// gcc SERVER_MESS.c -o server_mess -lsqlite3
