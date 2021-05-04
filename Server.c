/**********************************************************/
/* This program is a server that handles up to MAXGAMES at*/
/* a time. Server is player 1 and awaits new game requests*/
/* from the Client or player 2                            */
/**********************************************************/

#include <stdio.h>
#include <strings.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <net/if.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>

#define TIMETOWAITS 10 //Time for server refresh
#define TIMETOWAITG 30 //Time for Game resets/timeouts
#define ROWS  3
#define COLUMNS  3
#define NUMBEROFBYTESINMESSAGE 1
#define CURRENTVERSION 6 //Current protocol ver
#define NEWGAME 0 //Command for new game request
#define MOVE 1 //Command for move request
#define RESUME 3 //Command to resume playing a game
#define SIZEOFMESSAGE 5
#define MAXGAMES 5 //Maximum games server may host at a time
// DMO ERRORS
#define NOERROR 0
#define BADMOVE 1
#define GAMEOUTOFSYNC 2
#define INVALIDREQUEST 3
#define GAMEOVER 2 //Game over command 
#define GAMEOVERACK 5
#define BADVERSION 6
#define MAXCLIENTS 5

// Define the MC group
#define MC_PORT 1818
#define MC_GROUP "239.0.0.1"
/**********************************************************/
/* pre define all the functions we will write/call        */
/**********************************************************/

int checkwin(char board[ROWS][COLUMNS]);
void print_board(char board[ROWS][COLUMNS]);
int tictactoe(int sock, int playerNumber, char board[ROWS][COLUMNS], int playingGame, struct sockaddr_in *from);
int getMoveFromNet(int cd, char result[NUMBEROFBYTESINMESSAGE], int playingGame, struct sockaddr_in * from);
int createServerSocket(int portNumber, int *sock);
int createMCSocket(int portNumber, int *MC_sock);
int createClientSocket(char *ipAddress, int portno, struct sockaddr_in *sin_addr, int *sock);
int sendToNet(int sock, char move [4], struct sockaddr_in *to);
int initSharedState(char board[ROWS][COLUMNS]);
int isSquareTaken(int choice, char board[ROWS][COLUMNS]);

typedef struct{
  int gameNumber;   //Stores game number per client
  char lastMove[5]; //Stores last move sent by server to client
  char tempMove[5];  //Server stores last move received by client to reference seqnum
  int lastTime; //Stores last known time stamp after lastMove was sent OLD CODE NOT USED
  bool active;
  int resendcount;
  int clientMoves[10];
  int seqNum;
  char gboard[ROWS][COLUMNS];//stores individual board per game.
}GAME; //This structure holds all individual game information

void setData(GAME * game, char * d, int n); //This function sets the data for an individual game
int checkGame(int count,int s, GAME * games, int number, struct sockaddr_in * from);    //This function checks if a game is active or not
void resetGame(GAME * game, int n);         //This function resets an individual game
void setTemp(GAME * game, char * d, int n); //This function stores the recent value sent by the client

int main(int argc, char *argv[]) /* server program called with port # */
{
  /* variable definitions */
  int gamesPlayed=0;
  GAME * activeGames = malloc(MAXGAMES+2);//"GAME" pointer 
  int gameNumbers[]={0,0,0,0,0};
  char board[ROWS][COLUMNS];
  int row,column;
  int MC_sock, sock, Socket,rc,connected_sd;
  int portNumber, playerNumber;
  struct sockaddr_in from;
  struct sockaddr_in sin_addr; /* structure for socket name 
				* setup */
  int clientSDList[MAXCLIENTS]={0};//NEW
  fd_set socketFDS; //passing to the select command
  int i,maxSD = 0; //New
  int tempGamenum=0;
  if(argc < 3) { // means we didn't get enough parameters
    printf("usage: tictactoe <port_number> <1>\n");
    exit(1);
  }
  else if(argc == 3 && ((int)strtol(argv[2],NULL,10))==1){
    printf("Server starting : Player %s, Port %s\n", argv[2],argv[1]);
  }
  else{
    printf("Player 2 cannot be a server!!\n");
    exit(1);
  }    
  int playingGame = 0;
  /**********************************************************/
  /* convert the parameter for port and player number       */
  /**********************************************************/
  portNumber = strtol(argv[1], NULL, 10);
  playerNumber = strtol (argv[2], NULL, 10);
  
  //Here we reset all games before begining
  for(int i=0;i<MAXGAMES;i++)
    resetGame(activeGames,i);

  //Create socket and listen on it
  rc = createServerSocket(portNumber, &sock); // call a subroutine to create a socket
  //Create MC socket
  rc = createMCSocket(MC_PORT, &MC_sock); // call a subroutine to create a socket
  /***************************************************************************/
  /* If here then we have a valid socket. Init the state and get ready for   */
  /* an incoming newgame or move request. This will loop forever                  */
  /***************************************************************************/
  socklen_t fromLength = sizeof(struct sockaddr_in); // HAS to be set in linux...
  Socket = sock ; // DGRAM doesn't understand connections, but STREAM did, so using same variable
  maxSD = Socket;//NEW
  //MC_sock = socket(AF_INET,SOCK_DGRAM,0);

  while(1){ //loop forever and accept all comers for games!
    char data[40];
    char message[40];
    char result [SIZEOFMESSAGE];
    memset (data, 0, 40); // always zero out the data in C
    memset (result, 0, SIZEOFMESSAGE); // always zero out the data in C
    
    /*************************************************************/
    /* Below we begin to listen on the master socket and fill our*/
    /* "array" of other connections                              */
    /*************************************************************/
    FD_ZERO(&socketFDS);
    FD_SET(Socket,&socketFDS);
    FD_SET(MC_sock,&socketFDS);
    if(MC_sock>Socket){
      maxSD=MC_sock;
    }
    for(i=0;i<MAXCLIENTS;i++)
      if(clientSDList[i]>0){
        FD_SET(clientSDList[i],&socketFDS);
        if(clientSDList[i]>maxSD)
          maxSD = clientSDList[i];//Update our maxSD integer with current socket
      }
      printf("Sock = %d MC = %d maxSD = %d\n",Socket, MC_sock, maxSD);
      printf("waiting on select from main socket -> %d\n",Socket);
      //printf("waiting on select from MC socket -> %d\n",MC_sock);
      rc = select (maxSD+1, &socketFDS, NULL,NULL,NULL);
      printf("select popped rc: %d\n",rc);
    
    //Checking for hit in the MC sock
    if(FD_ISSET(MC_sock,&socketFDS)){//Accept new connection/socket and store below
      printf("MC_sock hit!\n");
      memset(message,0,40);
      rc = recvfrom(MC_sock, message, 3, 0,
                   (struct sockaddr *) &from, &fromLength);//MC recv
      printf("Received:%x,%x on MC socket\n",message[0],message[1]);
      if(rc < 0){
        printf("Error...\n");
        exit(1);
      }
      else if(message[0]!= CURRENTVERSION){
        printf("Bad version. . .\n");
        continue;
      }
      else if(playingGame<MAXGAMES){
        //char* replyC = (char*)malloc(3);
        char unimessage[40];
        short tempPort = htons(portNumber);
        unsigned char *replyC = (char *) &tempPort;
        memset(unimessage,' ',40);
        unimessage[0]=CURRENTVERSION;
        unimessage[1] = (char) replyC[0];
        unimessage[2] = (char) replyC[1];
        printf("Sending port: %d\n",portNumber);
        printf("Game Available!!!\n");
        playingGame++;
        for(int i=0;i<MAXGAMES;i++){ //Check to see if game space available
            if(gameNumbers[i]==0){
              data[3] = i;
              gameNumbers[i]=1;
              //printf("TEST %d\n",i);
              tempGamenum=i;
              break;
            }
          }
        setTemp(activeGames,data,data[3]);
        setData(activeGames,data,data[3]);
        activeGames[data[3]].gameNumber = tempGamenum;
        rc = sendto(MC_sock,unimessage,3,0,(struct sockaddr *) &from, fromLength);
      }
    }
    if(FD_ISSET(Socket,&socketFDS)){//Accept new connection/socket and store below
    //Same as above with MCsock new ifstatement
      connected_sd = accept (Socket,(struct sockaddr *)&from, &fromLength);
      if(connected_sd<0){
        printf("something went wrong...\n");
        break;
      }
      for(i=0;i<MAXCLIENTS;i++){
        if(clientSDList[i]==0){
          clientSDList[i] = connected_sd;//NEW stores new socket <--Here
          printf ("accepted a new socket %d pos %d\n", connected_sd, i);
          break;
        }
      }// end of loop for each client
    }
    for(i=0;i<MAXCLIENTS;i++){
      if(FD_ISSET(clientSDList[i],&socketFDS)){
        printf("received a move from the NET\n");
        rc = read(clientSDList[i],&data,SIZEOFMESSAGE);//TCP version 
        //rc=getMoveFromNet(clientSDList[i], data, playingGame,&from);//UDP version OLD
        if(rc<=0){
          printf("client left...\n");
          close(clientSDList[i]);
          clientSDList[i] = 0;
          resetGame(activeGames,data[3]);
          playingGame--;
          gameNumbers[data[3]]=0;
          continue;
        }
        //Received bad version protocol
        if (data[0] != CURRENTVERSION){
	  printf ("Bad version\n");
	  printf ("received %x %x\n", data[0], data[1]);
	  continue; // let's me skip to the top of the loop
        }
        //Received a move from the network
        if (data[1] == MOVE && data[4]>activeGames[data[3]].seqNum){//Check if the game number is a current game
          if(activeGames[data[3]].active==true){
            printf("Received: ver:%x com:%x pos:%x seq:%x from Game number: %x\n",data[0],data[1],data[2],data[4],data[3]);
            setTemp(activeGames,data,data[3]);
            activeGames[data[3]].clientMoves[data[2]]=1;
            row = (int)((data[2]-1) / ROWS);
            column = (data[2]-1) % COLUMNS;
            activeGames[data[3]].gboard[row][column] = 'O';
            //Check for a win
            rc = checkwin(activeGames[data[3]].gboard);
            if(rc==1){
              printf("GAME OVER\n");
              memset(result,0,SIZEOFMESSAGE);
              result[0]=CURRENTVERSION;
              result[1]=GAMEOVER;
              result[3]=data[3];
              result[4]=data[4]+1;
              resetGame(activeGames,data[3]);
              playingGame--;
              gameNumbers[data[3]]=0;
              //rc = sendToNet(clientSDList[i],result,&from);//Send first move to client
              //NEW TCP version for lab7
              rc = write(clientSDList[i], result, SIZEOFMESSAGE);
              printf("Sent %x,%x,%x,%d to game %d\n",result[0],result[1],result[2],result[4],result[3]);
            }
            else{
              result[0]=CURRENTVERSION;
              result[1]=MOVE;
              result[3]=data[3];
              result[4]=data[4]+1;
              for (int i=1;i<10;i++){ //Here we select a move to sent to player 2
                if(activeGames[data[3]].clientMoves[i]==0){
                  result[2]=i;
                  break;
                }
              }
              row = (int)((result[2]-1) / ROWS);
              column = (result[2]-1) % COLUMNS;
              activeGames[data[3]].gboard[row][column] = 'X';
              //Check for a win
              rc = checkwin(activeGames[data[3]].gboard);
              if(rc==1){
                printf("GAME OVER\n Waiting for ACK\n");
              }
              printf("Data to send: %x,%x,%x,%x,%x\n",result[0],result[1],result[2],result[3],result[4]);
              //NEW TCP version for lab7
              rc = write(clientSDList[i], result, SIZEOFMESSAGE);
              activeGames[data[3]].clientMoves[result[2]]=1;//record move on board
              setData(activeGames,result,data[3]);          //set data for corresponding game
            }
            printf("Sent %x,%x,%x to game %d\n",result[0],result[1],result[2],result[3]);
          }
          else
            printf("Game does not exist!!\n");
        }
      //Sequence number is 1 less than last move sent..Duplicate packet received...resends last known message
      else if(data[1] == MOVE && data[4]<=activeGames[data[3]].seqNum && (activeGames[data[3]].lastMove[4]-data[4])==1){
        printf("Ver:%x Com:%x Pos:%x Num:%x Seq:%x\n",data[0],data[1],data[2],data[3],data[4]);
        printf("Dup?..Resending previous packet\n");
        printf("Ver:%x Com:%x Pos:%x Num:%x Seq:%x\n",activeGames[data[3]].lastMove[0],activeGames[data[3]].lastMove[1],activeGames[data[3]].lastMove[2],activeGames[data[3]].lastMove[3],activeGames[data[3]].lastMove[4]);
        //rc = sendToNet(clientSDList[i],activeGames[data[3]].lastMove,&from);
        //NEW TCP version for lab7
        rc = write(clientSDList[i], activeGames[data[3]].lastMove,5);
      }
      //Sequence number received is higher than last sent
      else if(data[1] == MOVE && data[4]>activeGames[data[3]].seqNum && (data[4]-activeGames[data[3]].lastMove[4])>=1){
        printf("Game %d Out of sync...\n",data[3]);
        printf("Ignoring...\n");
      }
      //else{//TODO: is this else needed?
      //  printf("garbage data...\n");
      //}
      //Received a game request that server cannot handle
      if (playingGame >=MAXGAMES && data[1] == NEWGAME){
	printf ("Already playing %d games!!\n",MAXGAMES);
	continue; // error - not enough games available
      }
      //New game request from network
      else if (playingGame<MAXGAMES && data[1]==NEWGAME && data[2]==0 && data[4]==0 && data[3]==0){
	printf ("received game request with right version %x %x %x %x %x \n", data[0],data[1],data[2],data[3],data[4]);
        if(playingGame<MAXGAMES){  
          for(int i=0;i<MAXGAMES;i++){ //Check to see if game space available
            if(gameNumbers[i]==0){
              data[3] = i;
              gameNumbers[i]=1;
              break;
            }
          }
          setTemp(activeGames,data,data[3]);
          data[1]=1;
          data[2]=1;//First move sent to game
          setData(activeGames,data,data[3]);    //call to Function to set data to corresponding game
          activeGames[data[3]].clientMoves[1]=1;//Update moves on current board
        }
        if(!isSquareTaken(data[2],activeGames[data[3]].gboard)){
          data[4]=activeGames[data[3]].seqNum+1;
          //rc = sendToNet(clientSDList[i],data,&from);//Send first move to client
          //TCP version for lab7
          rc = write(clientSDList[i],data,SIZEOFMESSAGE);
          printf("Sent %x,%x,%x,%x to game %d\n",data[0],data[1],data[2],data[4],data[3]);
          playingGame++;//Add to games currently being played
          activeGames[data[3]].resendcount=0;
          row = (int)((data[2]-1) / ROWS);
          column = (data[2]-1) % COLUMNS;
          activeGames[data[3]].gboard[row][column] = 'X';
	}
      }
      else if(data[1]==GAMEOVER && activeGames[data[3]].active==true){
        printf("Received a gameover command from Game %d\nReseting game %d...\n",data[3],data[3]);
        printf("Received %x,%x,%x,%x,%d\n",data[0],data[1],data[2],data[3],data[4]);
        printf("Game Over!!\n");
        char endPack[40];
        memset(endPack,0,40);
        endPack[0]=CURRENTVERSION;
        endPack[1]=GAMEOVER;
        endPack[3]=data[3];
        endPack[4]=data[4]+1;
        resetGame(activeGames,data[3]);
        playingGame--;
        gameNumbers[data[3]]=0;
        //rc = sendToNet(clientSDList[i],endPack,&from);//Send first move to client
        //TCP version for lab7
        rc = write(clientSDList[i], endPack, SIZEOFMESSAGE);
        printf("Sent %x,%x,%x,%d to game %d\n",endPack[0],endPack[1],endPack[2],endPack[4],endPack[3]);
      }
      else if(data[1]==RESUME){
        char tempb[ROWS][COLUMNS];
        rc = read(clientSDList[i],&tempb,9);
        printf("Received %x,%x,%x,%x,%d\n",data[0],data[1],data[2],data[3],data[4]);
        int count = 1;
        for (int i=0;i<3;i++)
          for (int j=0;j<3;j++){
            activeGames[tempGamenum].gboard[i][j] = tempb[i][j];
            if(tempb[i][j]=='X'||tempb[i][j]=='O'){
              activeGames[tempGamenum].clientMoves[count]=1;
              count++;
            }
          }
        print_board(activeGames[tempGamenum].gboard);
        for (int i=1;i<10;i++){ //Here we select a move to sent to player 2
          if(activeGames[tempGamenum].clientMoves[i]==0){
            result[2]=i;
            break;
          }
        }
        result[0]=CURRENTVERSION;
        result[1]=MOVE;
        result[3]=tempGamenum;
        result[4]=1;//Reseting seq num for client game
        setData(activeGames,result,tempGamenum);
        setTemp(activeGames,result,tempGamenum);
        rc = write(clientSDList[i], result, SIZEOFMESSAGE);
        printf("Sent %x,%x,%x,%d to game %d\n",result[0],result[1],result[2],result[4],result[3]);
      }
      for (int i=0; i<MAXGAMES; i++){
        printf("Game%d recent data: %x,%x,%x\n",i,activeGames[i].lastMove[0],activeGames[i].lastMove[1],activeGames[i].lastMove[2]);
        printf("\n");//Formatting 
      }
    } // end of the infinite while loop for player 1
  } // end of the if player==1 statement
}
  return 0;
}

//Sets the most recent move to the corresponding game
void setData(GAME * game, char * d, int n){
  game[n].lastMove[0] = d[0];
  game[n].lastMove[1] = d[1];
  game[n].lastMove[2] = d[2];
  game[n].lastMove[3] = d[3];
  game[n].lastMove[4]=d[4];
  game[n].active = true;
  game[n].gameNumber = n;
  game[n].seqNum = d[4];
  //memset(game[n].gboard,0,sizeof(game[n].gboard));
}

//Sets the most recent move from client to server
void setTemp(GAME * game, char * d, int n){
  game[n].tempMove[0] = d[0];
  game[n].tempMove[1] = d[1];
  game[n].tempMove[2] = d[2];
  game[n].tempMove[3] = d[3];
  game[n].tempMove[4] = d[4];
  //game[n].seqNum=d[4];
}

//Returns if there is a game available or not
int checkGame(int count,int s, GAME * game, int number, struct sockaddr_in * from){
  time_t t = time(0);
  time(&t);
  int temp = (int)t;
  int dif;
  int c = count; 
  dif = temp-game[number].lastTime;
  //Doesnt work properly
  
  if(dif>TIMETOWAITG && c<3){
    while(c<3){
    printf("It's quiet..\nResending last move...attempt %d\n",count);
    c++;
    //game[number].resendcount++;
    //sendToNet(s,game[number].lastMove,from);
    write(s,game[number].lastMove,5);
    }
    return 1;
  }
  if(dif>TIMETOWAITG&&c>=3){
    printf("Reseting Game %d\n",number);
    resetGame(game,number);
    return 0;
  }
  else
    return 1;
}

//Resets the corresponding game
void resetGame(GAME * game, int n){
  game[n].lastMove[0] = 0;
  game[n].lastMove[1] = 0;
  game[n].lastMove[2] = 0;
  game[n].lastMove[3] = 0;
  game[n].lastTime=0;
  for(int i=1;i<10;i++){
    game[n].clientMoves[i]=0;
  }
  game[n].active=false;
  memset(game[n].gboard,0,sizeof(game[n].gboard[ROWS][COLUMNS]));
  initSharedState(game[n].gboard);
}
//Old function not used by current Server protocol CURRENTVERSION
int tictactoe(int sock, int playerNumber, char board[ROWS][COLUMNS], int playingGame, struct sockaddr_in *from)
{ 
  int player = 1, i, choice;
  int row, column;
  char mark;
  int rc;
  char junk[10000];
  char result [SIZEOFMESSAGE];
  int moveCount = 0;
  //  int iWon = 0;

  
  memset (result, 0, SIZEOFMESSAGE); //always zero out the data in C
  
  do{
    print_board(board);
    player = (player % 2) ? 1 : 2;
    /******************************************************************/
    /* If it is 'this' player's turn, ask the human for a move, then  */
    /* send the move to the other player. If it is not this player's  */
    /* turn, then wait for the other player to SEND you a move        */
    /******************************************************************/
    
    if (player == playerNumber){
      do{
        printf("Player %d, enter a number:  ", player);
        rc = scanf("%d", &choice); // get input
        if (rc ==0){ 
          choice = 0;// cleanup needed bad input
          printf ("bad input trying to recover\n");
          rc = scanf("%s", junk);
          if (rc ==0){
            /* they entered more than 10000 bad characters, quit) */
            printf ("garbage input\n");
            return 1;
          }
        }
      }while ( ((choice < 1) || (choice > 9)) || (isSquareTaken (choice, board))); // loop until good data
      
      mark = (player == 1) ? 'X' : 'O'; // determine which player is playing, set mark accordingly
      printf ("tictactoe: playerNumber is %d, player is %d\n", playerNumber, player);
      printf ("tictactoe: mark is '%c'\n", mark);
      
      /******************************************************************/
      /*  little math here. you know the squares are numbered 1-9, but  */
      /* the program is using 3 rows and 3 columns. We have to do some  */
      /* simple math to conver a 1-9 to the right row/column            */
      /******************************************************************/
      row = (int)((choice-1) / ROWS);
      column = (choice-1) % COLUMNS;
      
      board[row][column] = mark;

      
      result [0] = CURRENTVERSION;
      result [1] = MOVE;
      result [2] = (char)choice ;

#ifdef OSUCODE      
      result [2] = (char)choice + 0x30;
#endif
      
      moveCount = moveCount +2; // not used...yet
      
      i = checkwin(board);
      
      switch (checkwin(board)){
         case 0:
         case 1:
           printf ("found a winner, its me!\n");
           break;
      }
      
      printf ("tictactoe: sending hex value of %x  %xto network\n", result[0], result[1]);
      sendToNet(sock, result, from); // send it to the network 
    }
    else{ // this 'else' means that it is the other player's move (network)
      choice = getMoveFromNet(sock, result, playingGame, from); // Wait for move from the network
      printf ("tictactoe: received move %d\n", choice);
      if (choice == -1){
        printf ("tictactoe: move indicates timeout\n");
        return -1; //timeout
      }
      if (isSquareTaken (choice, board)){
        printf ("tictactoe - bad move '%d' from other side, exiting out \n", choice);
        return -1; // can't happen YET
      }
      if (choice <=0){
        printf ("did the other side die?\n");
        return -1; // timeout dup of above
      }
    } // end of 'else' - got move from network
    
    mark = (player == 1) ? 'X' : 'O'; // determine which player is playing, set mark accordingly
    /******************************************************************/
    /*  little math here. you know the squares are numbered 1-9, but  */
    /* the program is using 3 rows and 3 columns. We have to do some  */
    /* simple math to conver a 1-9 to the right row/column            */
    /******************************************************************/
    row = (int)((choice-1) / ROWS);
    column = (choice-1) % COLUMNS;
    
    board[row][column] = mark;
    
    /* check is someone won */
    i = checkwin(board);
    player++; // change the player number
  }while (i ==  - 1);

  
  /* print the board one last time */
  print_board(board);
  
  /* check to see who won */
  if (i == 1)
    printf("==>\aPlayer %d wins\n ", --player);
  else
    printf("==>\aGame draw");
  
  return 0;
}

//old code not used by current server protocol CURRENTVERSION
int checkwin(char board[ROWS][COLUMNS])
{
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

    return 0;
  else
    return  - 1;
}


void print_board(char board[ROWS][COLUMNS])
{

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

 
int  createServerSocket(int portNumber, int *sock){
  /*****************************************************************/
  /* create a server socket and bind to the 'portNumber' passed in */
  /*****************************************************************/
  struct sockaddr_in name;
  struct sockaddr_in from_address;
  socklen_t fromLength;
  int rc;
  /*create socket*/
  *sock = socket(AF_INET, SOCK_STREAM, 0);
  if(*sock < 0) {
    perror("opening datagram socket");
    exit(1);
  }
  /* fill in name structure for socket, based on input   */
  name.sin_family = AF_INET;
  name.sin_port = htons(portNumber);
  name.sin_addr.s_addr = INADDR_ANY;
  if(bind(*sock, (struct sockaddr *)&name, sizeof(name)) < 0) {
    perror("getting socket name");
    exit(2);
  }
  rc = listen(*sock,5);
  if(rc<0){
    perror("error with listen");
    exit(1);
  }
  
  return 1;

}

int createMCSocket(int portNumber, int *MC_sock){
  struct sockaddr_in addr;
  socklen_t addrlen;
  struct ip_mreq mreq;

  /* set up MulitCast socket */
  *MC_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (*MC_sock < 0) {
    perror("socket");
    exit(1);
  }
  bzero((char *)&addr, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(MC_PORT);
  addrlen = sizeof(addr);

  if (bind(*MC_sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
    perror("bind");
    exit(1);
  }
  mreq.imr_multiaddr.s_addr = inet_addr(MC_GROUP);
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  if (setsockopt(*MC_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                 &mreq, sizeof(mreq)) < 0) {
    perror("setsockopt mreq");
    exit(1);
  }
  return 1;
}

int getMoveFromNet(int sd, char result[SIZEOFMESSAGE], int playingGame, struct sockaddr_in * from)
{
  /*****************************************************************/
  /* instead of getting a move from the user, get if from the net  */
  /* by doing a receive on the socket. this is all still STREAM    */
  /*****************************************************************/
  int movePosition;
  int rc;
  struct timeval tv;

  int fromLength = sizeof(struct sockaddr_in);

  //DMO  unsigned int ret;
  //DMO  char *data = (char*)&ret;
  char *data = result;
  //  int left = sizeof(ret);
  tv.tv_sec = TIMETOWAITS;
  tv.tv_usec = 0;
  if (setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)))
  //  perror ("error"); // might want to exit here   
  

  do {
    //rc = recvfrom(sd, data, SIZEOFMESSAGE, 0, (struct sockaddr *) from, (socklen_t *)&fromLength);
    rc = read(sd,data,40);//NEW TCP read
    if (rc <0){
      printf ("getMoveFromNet: timeout occured\n");
      return -1;
    }
    // have to handle timeout still. if timeout and playing game, return from here
    printf ("getMoveFromNet: received rc %d, hex %x %x\n", rc, data[0], data[1]);
    if (data[0] != CURRENTVERSION){
      printf ("Bad version\n");
      continue;
    }
    //if (playingGame == 1 && data[1] == NEWGAME){
    //  printf ("received a newgame request, ignore it\n");
    //  continue;
    //}
    else if (data[1] == MOVE){
      printf ("getMoveFromNet: received  %x %x %x the network \n",data[0], data[1], data[2]);
      movePosition = (int)data[2];

//#ifdef OSUCODE
      //movePosition -= 0x30;
//#endif
      return (movePosition);
    }
  }
  while (1);
    
  return -1; // should never get here
}

int sendToNet(int sock, char result[SIZEOFMESSAGE], struct sockaddr_in * to){
  /*****************************************************************/
  /* This function is used to send data to the other side. make    */
  /* sure data is in network order.                                */
  /*****************************************************************/
  
  //  char data[NUMBEROFBYTESINMESSAGE];

  char * data = result;
  int namelen = sizeof (struct sockaddr_in);

  /*****************************************************************/
  /* Write data out.                                               */
  /* loop to make sure all the data is sent                        */
  /*****************************************************************/

  //From Datagram OLD CODE
/*
  if(sendto(sock, data, SIZEOFMESSAGE, 0, (struct sockaddr *)to, namelen) < 0) {
    perror("error sending datagram");
    exit(5);
  }
*/

  //TCP socket changes
  if(write(sock,data,SIZEOFMESSAGE)<0){
    perror("error sending message");
    exit(5);
  }

  return 0;
}

int initSharedState(char board[ROWS][COLUMNS]){    
  /*****************************************************************/
  /* This function is used to initialize the shared board          */
  /*****************************************************************/
  int i, j, count = 1;
  //printf ("in sharedstate area\n");
  for (i=0;i<3;i++)
    for (j=0;j<3;j++){
      board[i][j] = count + '0';
      //    printf("square [%d][%d] = %c\n", i,j,board[i][j]);
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
  if (board[row][column] == (choice+'0'))
    return 0; // 0 means the square is free
  else
    {
      printf("Invalid move ");
      return 1; // means the square is taken
    }
}
