//Project for multicast
//Client will be provided a multicast group defined, one in file
//client will connect with stream, start game, game gets dropped, i receive no data
//i send out to multicast group with datagram that i want a new game
//server replies with a datagram with a port, ip address is in a field
//i go back and connect through stream to that server
//after resuming i provide 5byte stream then 9 bytes for positions already played
//then resume game.


#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdbool.h>


#define ROWS  3
#define COLUMNS  3
#define TIMETOWAIT 30
#define VERSION 6  //last version after 7
#define PLAYING 1
#define GAMEOVER 2
#define NEWGAME 0
#define RESUME 3
#define SIZEOFMESSAGE 5
#define MOVE 1

//Defining multicast group
#define MC_PORT 1818
#define MC_GROUP "239.0.0.1"

//Defining functions
int checkwin(char board[ROWS][COLUMNS]);
void print_board(char board[ROWS][COLUMNS]);
int tictactoe();
int initSharedState(char board[ROWS][COLUMNS]);
bool checkGameMove(int);
int isSquareTaken(int choice, char board[ROWS][COLUMNS]);
int getMoveFromNet(int sd, char result[SIZEOFMESSAGE], int playingGame, struct sockaddr_in * from, bool firstSequence, int sequence, char board[ROWS][COLUMNS]);


//Checking proper args, setting up socket
//connecting to server and starting game.
int main(int argc, char *argv[])
{
  int playerNum;
  char IP[29];
  int PORT;
  char board[ROWS][COLUMNS]; 
  int rc, ld, sd;     //return code from recvfrom

  struct sockaddr_in server_address;
  socklen_t fromLength;




  //Here  we check for the proper user provided arguments 
  //TODO:Check args here for deciding Server or Client
  if(argc != 4 ){
    printf("Not enough args.\nIP PORT Player#\n");
    return 1;
  }
  else if (argc == 4 && ((int)strtol(argv[3],NULL,10))==2){
    strcpy(IP, argv[1]);
    printf("Client starting : Player %s, IP %s Port %s\n", argv[3], argv[1], argv[2]);
  }
  else{
    printf("Invalid arguments\nIP PORT Player#\n");
    return 1;
  }

  //struct sockaddr_in myAddress;

  strcpy(IP, argv[1]);
  PORT = strtol(argv[2],NULL, 10);
  playerNum = atoi(argv[3]);


  //For the project I connect first through TCP
  if((sd = socket (AF_INET, SOCK_STREAM, 0)) < 0){
    perror("Error opening stream socket.\n");
    exit(1);
  }

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(PORT);
  server_address.sin_addr.s_addr = inet_addr(IP);



  /*
  //changed to sock stream for LAB7
  if((sd = socket (AF_INET, SOCK_STREAM, 0)) < 0){
    perror("Error opening stream socket.\n");
    exit(1);
  }
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(PORT);
  server_address.sin_addr.s_addr = inet_addr(IP);

  

  //lab 6
  myAddress.sin_family = AF_INET;
  PORT += 1;
  myAddress.sin_port = htons(PORT);
  myAddress.sin_addr.s_addr = INADDR_ANY;
  
  //****These are the lines needed to be commented out****************
  if(bind(sd, (struct sockaddr *)&myAddress, sizeof(struct  sockaddr)) < 0 ){
    perror("Didn't get socket name.\n");
    exit(2);
  }
  */
  


  /*
  //connect for lab 7
  if (connect(sd, (struct sockaddr *) &server_address, sizeof(struct sockaddr_in)) < 0) {
  	close(sd);
  	perror("Something went wrong.");
  	exit(1);
  }
  */
  

  rc = initSharedState(board); // Initialize the 'game' board
  rc = tictactoe(sd, board, playerNum, &server_address); // call the 'game' 
  
  return 0; 
}//main


//Game is played here for moves
int tictactoe(int socket, char board[ROWS][COLUMNS],int player, struct sockaddr_in *saddr)
{

  //I need to establish the connection inside the tictactoe game to change the connection
  //after a multicast server is found and I'm given an IP/Port


  //i need to connect in here
  //idk if its &saddr *saddr or saddr
  if (connect(socket, (struct sockaddr *) &saddr, sizeof(struct sockaddr_in)) < 0) {
    close(socket);
    perror("Something went wrong.");
    exit(1);
  }


  int i, n, len, t, choice, gameNumber;
  int row, column;
  int mySequence = 0;//used to keep track of move order for players
  bool firstMove = (player % 2) ? true : false;
  char newGame[SIZEOFMESSAGE], gameMove[SIZEOFMESSAGE], buf[SIZEOFMESSAGE], junk[10000];
  char mark;// either an 'X' or an '0'
  int rc;//Used to keep track of TCP reads and writes

  //Creating a newGame buffer for future use.
  memset(newGame, 0, SIZEOFMESSAGE);
  newGame[0] = VERSION; //version
  newGame[1] = NEWGAME; //game mode
  newGame[2] = 0; // position
  newGame[3] = 0; // game number
  newGame[4] = mySequence; //sequence number should be zero

  //sendto(socket, newGame, SIZEOFMESSAGE, 0, (struct sockaddr *) saddr, sizeof(struct sockaddr_in));
  rc = write(socket, newGame, SIZEOFMESSAGE);
  printf("New game was sent to server.\n");

  do{
    print_board(board); // call function to print the board on the screen
    player = (player % 2) ? 1 : 2;  // Mod math to figure out who the player is

    //mySequence starts at 0 for new game.
    mySequence += 1;
    printf("Sequence %d\n", mySequence);


    //If firstMove is true client makes a move on keyboard.
    if(firstMove){
    	char z;
    	memset(gameMove, 0, SIZEOFMESSAGE);
    	
      do{
        printf("Player 2 enter a number:  ");
        rc = scanf("%d", &choice); // get input
        if (rc == 0){ 
          choice = 0;// cleanup needed bad input
          printf ("Bad input trying to recover\n");
          rc = scanf("%s", junk);
          if (rc == 0){
        /* they entered more than 10000 bad characters, quit) */
            printf ("Garbage input\n");
            return 1;
          }
        }
      }while ( ((choice < 1) || (choice > 9)) || (isSquareTaken (choice, board))); // loop until good data

      //Got client choice, sending move.
    	gameMove[0] = VERSION;
    	gameMove[1] = PLAYING; //00 newgame : 01 playing : 02 game over
    	gameMove[2] = choice; //changes every move
      gameMove[3] = gameNumber; //should not change after set
      gameMove[4] = mySequence;

    	//sendto(socket, gameMove, SIZEOFMESSAGE, 0, (struct sockaddr *) saddr, sizeof(struct sockaddr_in));
      write(socket, gameMove, SIZEOFMESSAGE);

    }else{
      //Server needs to send a move, check from net
      //Give them 3 tries to send a valid response.
      int serverSequence;
      bool newGamecheck = (mySequence == 1) ? true : false;  

      choice = getMoveFromNet(socket, buf, 1, saddr, newGamecheck, mySequence, board);
      
      if(mySequence == 1){
        gameNumber = buf[3];
      } //your number game for server
      
      serverSequence = buf[4]; //sequence number to check

      int y = 3;//allow for 3 bad moves
      //Check mySequence to serverSequence, first move should be 1 = 1
      while(serverSequence != mySequence){
        if(serverSequence == mySequence - 2){
          //at this point gameMove buffer should be filled with what we last sent..
          gameMove[4] = mySequence - 1;  

          //sendto(socket, gameMove, SIZEOFMESSAGE, 0, (struct sockaddr *) saddr, sizeof(struct sockaddr_in));
          write(socket, gameMove, SIZEOFMESSAGE);
          printf("Resent last sequence %d\n", mySequence);
          //i sent and then i want to recv again....
        }else if(y == 0){
          printf("We gave the server enough tries. Closing game.\n");
          return -1;
        }else if(newGamecheck == true){
          printf("Waiting for new game resending newgame.\n");
          write(socket, newGame, SIZEOFMESSAGE);
          //sendto(socket, newGame, SIZEOFMESSAGE, 0, (struct sockaddr *) saddr, sizeof(struct sockaddr_in));

        }
        y--;
        printf("Waiting to receive a valid sequence.\n%d tries left.\n", y);

        choice = getMoveFromNet(socket, buf, 1, saddr, false, mySequence, board);
        if(mySequence == 1){
          gameNumber = buf[3];
        }
       //your number game for server
        serverSequence = buf[4]; //sequence number to check
      }

      //Getting here means we got a valid buffer
      //Need to check the choice they made.
      printf ("Received move %d\n", choice);
      printf("Server sent sequence %d\n", serverSequence);

      if (choice == -1){
        printf ("Move indicates timeout\n");
        return -1; //timeout
      }
      if (isSquareTaken (choice, board)){
        printf ("Bad move '%d' from other side, exiting out \n", choice);
        return -1; // can't happen YET
      }
      if (choice <=0){
        printf ("Did the other side die?\n");
        return -1; // timeout dup of above
      }      
    }
    
    mark = (firstMove) ? '0' : 'X';


    /******************************************************************/
    /** little math here. you know the squares are numbered 1-9, but  */
    /* the program is using 3 rows and 3 columns. We have to do some  */
    /* simple math to conver a 1-9 to the right row/column            */
    /******************************************************************/
    row = (int)((choice-1) / ROWS); 
    column = (choice-1) % COLUMNS;
    /* first check to see if the row/column chosen is has a digit in it, if it */
    /* square 8 has and '8' then it is a valid choice                          */
    if (board[row][column] == (choice + '0')){
    	board[row][column] = mark;
    }else if(board[row][column] == 'X' || board[row][column] == '0'){
      printf("Position already played.\n");
    }else{
    	printf("Invalid move ");
    	player--;
    	getchar();
    }
    /* after a move, check to see if someone won! (or if there is a draw */
    firstMove = (firstMove) ? false : true; 

    i = checkwin(board);
    player++;
  }while (i ==  - 1); // -1 means no one won

  //***GAME IS FINISHED***
  //Will check for client or server winner and ack game over.
  print_board(board);
  int checkWinner = (firstMove) ? 1 : 2;

  if (i == 1){ // means a player won!! congratulate them
    int y = 0;
    if(checkWinner == 1){
      printf("Server won.\n");
      //server won send gameover to ack
      gameMove[1] = GAMEOVER;
      //sendto(socket, gameMove, SIZEOFMESSAGE, 0, (struct sockaddr *) saddr, sizeof(struct sockaddr_in));
      write(socket, gameMove, SIZEOFMESSAGE);
      printf("Sending gameover command.\n");
    }else{
      //client won
      int ignore = getMoveFromNet(socket, buf, 1, saddr, false, mySequence, board);
      while(buf[1] != 02){
        y++;
        ignore = getMoveFromNet(socket, buf, 1, saddr, false, mySequence, board);
        printf("Checking for gameover command.\n");
        if(y == 3){
          printf("Never received game over command. \nWe'll assume we won.\n");
          buf[1] = 02;
        }
      }
      printf("Client won.\n");
    }
  }else{
    printf("==>\aGame was a draw.\n"); // ran out of squares, it is a draw
    
    write(socket, gameMove, SIZEOFMESSAGE);
    //sendto(socket, gameMove, SIZEOFMESSAGE, 0, (struct sockaddr *) saddr, sizeof(struct sockaddr_in));
  }
return 0;
}


int checkwin(char board[ROWS][COLUMNS])
{
  /************************************************************************/
  /* brute force check to see if someone won, or if there is a draw       */
  /* return a 0 if the game is 'over' and return -1 if game should go on  */
  /************************************************************************/
  if (board[0][0] == board[0][1] && board[0][1] == board[0][2] ) // row matches
    return 1;
  else if (board[1][0] == board[1][1] && board[1][1] == board[1][2] ) // row matches
    return 1;
  else if (board[2][0] == board[2][1] && board[2][1] == board[2][2] ) // row matches
    return 1;
  else if (board[0][0] == board[1][0] && board[1][0] == board[2][0] ) // column
    return 1;
  else if (board[0][1] == board[1][1] && board[1][1] == board[2][1] ) // column
    return 1;
  else if (board[0][2] == board[1][2] && board[1][2] == board[2][2] ) // column
    return 1;
  else if (board[0][0] == board[1][1] && board[1][1] == board[2][2] ) // diagonal
    return 1;
  else if (board[2][0] == board[1][1] && board[1][1] == board[0][2] ) // diagonal
    return 1;
  else if (board[0][0] != '1' && board[0][1] != '2' && board[0][2] != '3' &&
     board[1][0] != '4' && board[1][1] != '5' && board[1][2] != '6' && 
     board[2][0] != '7' && board[2][1] != '8' && board[2][2] != '9')
    return 0; // Return of 0 means game over
  else
    return -1; // return of -1 means keep playing
}

void print_board(char board[ROWS][COLUMNS])
{
  /*****************************************************************/
  /* brute force print out the board and all the squares/values    */
  /*****************************************************************/
  printf("\n\n\n\tCurrent TicTacToe Game\n\n");
  printf("Player 1 (X)  -  Player 2 (O)\n\n\n");
  printf("     |     |     \n");
  printf("  %c  |  %c  |  %c \n", board[0][0], board[0][1], board[0][2]);
  printf("_____|_____|_____\n");
  printf("     |     |     \n");
  printf("  %c  |  %c  |  %c \n", board[1][0], board[1][1], board[1][2]);
  printf("_____|_____|_____\n");
  printf("     |     |     \n");
  printf("  %c  |  %c  |  %c \n", board[2][0], board[2][1], board[2][2]);
  printf("     |     |     \n\n");
}

int initSharedState(char board[ROWS][COLUMNS]){    
  /* this just initializing the shared state aka the board */
  int i, j, count = 1;
  for (i=0;i<3;i++)
    for (j=0;j<3;j++){
      board[i][j] = count + '0';
      count++;
    }
  return 0;
}

int isSquareTaken(int choice, char board[ROWS][COLUMNS]){
  /******************************************************************/
  /*  little math here. you know the squares are numbered 1-9, but  */
  /* the program is using 3 rows and 3 columns. We have to do some  */
  /* simple math to conver a 1-9 to the right row/column            */
  /******************************************************************/
  int row, column; 

  row = (int)((choice-1) / ROWS);
  column = (choice-1) % COLUMNS;

  /* see if square is 'free' */
  if (board[row][column] == (choice + '0'))
    return 0; // 0 means the square is free
  else
    {
      printf("Invalid move ");
      return 1; // means the square is taken
    }
}

int getMoveFromNet(int sd, char result[SIZEOFMESSAGE], int playingGame, struct sockaddr_in *from, bool t, int sequence, char board[ROWS][COLUMNS])
{
  /*****************************************************************/
  /* instead of getting a move from the user, get if from the net  */
  /* by doing a receive on the socket. this is all still STREAM    */
  /*****************************************************************/
  int movePosition;
  int rc, ld;
  char temp[SIZEOFMESSAGE];


  char *data = result;


  memset(temp, 0, SIZEOFMESSAGE);
  temp[0] = VERSION; //protocol version
  temp[1] = NEWGAME; //command
  
  
  //For timeouts
  struct timeval tv;
  tv.tv_sec = TIMETOWAIT;
  tv.tv_usec = 0;
  if (setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)))
    perror ("error"); // might want to exit here   
  

  do {
    //rc = recvfrom(sd, data, SIZEOFMESSAGE, 0, (struct sockaddr *) from, (socklen_t *)&fromLength);
    rc = read(sd, data, SIZEOFMESSAGE);

    if (rc <= 0){
      char ack_port[4];
      //we only need multicast group if we lose tcp connection
      socklen_t addrlen;
      struct sockaddr_in mg_addr;
      bzero((char *)&mg_addr, sizeof(mg_addr));  
      mg_addr.sin_family = AF_INET;  
      mg_addr.sin_port = htons(MC_PORT);  
      mg_addr.sin_addr.s_addr = inet_addr(MC_GROUP);  
      addrlen = sizeof(mg_addr);

      char boardInfo[9];
      char getServer[SIZEOFMESSAGE];
      char bcast[SIZEOFMESSAGE];
      bcast[0] = VERSION;
      bcast[1] = RESUME;

      //to broadcast
      int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);

      //check for 0
      //send message on the multicast group to see if anyone is there
      //then recvfrom, ip and port, then go back and connect 
      printf ("Did we lose the server? Lets check the multicast group\n");
      sendto(udp_sock, bcast, sizeof(bcast), 0, (struct sockaddr*) &mg_addr, addrlen);

      //At this point i sent out to server
      //I need to get ip and port info in their message
      rc = recvfrom(udp_sock, getServer, SIZEOFMESSAGE, 0, (struct sockaddr *) from, &addrlen);
      if(rc > 0){
        //from sockaddr is my old tcp socket, im refreshing with the recvfrom ip and conecting to that
        from->sin_family = AF_INET;
        from->sin_port = ntohs(getServer[1]);
        printf("We got infomration from multicast, %c %c\n", getServer[0], getServer[1]);
        //Do i need this one?
        //from->sin_addr.s_addr = inet_ntoa(from->sin_addr.s_addr);
      }

      int x = 0;
      //send data now
      for(int i = 0; i<ROWS; i++){
        for(int j = 0; j<COLUMNS; j++){
          boardInfo[x] = board[i][j];
          x++;
        }
      }
    

      if (connect(sd, (struct sockaddr *) &from, sizeof(struct sockaddr_in)) < 0) {
        close(sd);
        printf("We failed to TCP connect to multicast server. Shutting down.\n");
        exit(1);
      }
      //sending board data
      ld = write(sd, boardInfo, 9);

      //we should resume game from here
      }//if (rc <= 0)

    // have to handle timeout still. if timeout and playing game, return from here
    if (data[0] != VERSION){
      printf ("Bad version\n");
      continue;
    }else if(data[1] == GAMEOVER){
        //sendto(sd, data, SIZEOFMESSAGE, 0, (struct sockaddr *) from, sizeof(struct sockaddr_in));
      write(sd, data, SIZEOFMESSAGE);
      return 1;
    }
    if(t == true && rc < 0){
      printf("Sent newgame and haven't received data. Resending newgame.");
      //sendto(sd, temp, SIZEOFMESSAGE, 0, (struct sockaddr *) from, sizeof(struct sockaddr_in));
      write(sd, temp, SIZEOFMESSAGE);
      continue;
    }
    else if (data[1] == MOVE){
      printf ("Received %x %x %x %x %x from the network\n",data[0], data[1], data[2], data[3], data[4]);
      movePosition = (int)data[2];
      return (movePosition);
    }
  }//do
  while (1);

  return -1; // should never get here
}
