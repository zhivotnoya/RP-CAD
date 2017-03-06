/*
    C socket server example, handles multiple clients using threads
*/
 
#include<stdio.h>
#include<string.h>          //strlen
#include<stdlib.h>          //strlen
#include<pwd.h>
#include<limits.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<locale.h>
#include<sys/socket.h>
#include<arpa/inet.h>       //inet_addr
#include<unistd.h>          //write
#include<pthread.h>         //for threading , link with lpthread
#include<sqlite3.h>         //for local database handling
#include<time.h>            //used for timestamping
#include<signal.h>          //used to catch Ctrl-C and other things
#include<err.h>             //used to handle errors?
#include<errno.h>
#include<ncurses.h>         //so that the server has an interface
#include<curses.h>

// definitions

char *month[12] = {
	"Jan",
	"Feb",
	"Mar",
	"Apr",
	"May",
	"Jun",
	"Jul",
	"Aug",
	"Sep",
	"Oct",
	"Nov",
	"Dec" };
    
char *errorlevel[3] = {
	"INFO",
	"WARN",
	"CRIT" };

// other defs
#ifndef LINE_MAX
#define LINE_MAX 1024
#endif

// program commands
#define EXITPROG ".quit"
#define MEMUSAGE ".ver"
#define UPTIME ".uptime" 



// other stuff

static volatile bool keepRunning = true;
static WINDOW *winp, *wout;
static int winrows, wincols;


enum { DATETIME, ERRLEVEL, MSG, WINP, };

// structures

struct Header
{
    char is_data;           // 1 for data packet, 0 for ack
    int fragment;           // fragment number (zero indexed)
    char last;              // 1 for last packet in msg, 0 otherwise
    char *uname;            // username that client is running under, for db access reasons
    int datalen;            // length of data in bytes - used only for last packet.
} HEADER;

struct citation
{
    char date[6];           // date of citation
    char *pc;               // penal code
    char *pclong;           // penal code description
    char *badge;            // officer's badge number
    char *location;         // location of citation
    int fine;               // amount of fine
} CITATION;

struct NCIC
{
    char *fn;               // first name
    char *ln;               // last name
    char *street;           // street name
    char *city;             // city name
    char state[2];          // state (FL=Florida, SA=San Andreas, LS=Liberty State, NY=North Yankton)
    int warrants;           // number of active warrants
    int citations;          // number of citations
    struct CITATION *citelist[];   // list of citations (if any)
};

struct DMV
{
    char *make;             // vehicle make
    char *model;            // vehicle model
    char *regFN;            // registered owner's first name
    char *regLN;            // registered owner's last name
    int parkTickets;        // number of parking tickets
    struct CITATION *pkTickList[];  // list of parking tickets (if any)
};

struct Packet
{
    struct HEADER *header;          // packet header
    int pktType;            // 1-NCIC, 2-VEH, 3-WARRANT, etc etc
    char pktMsg;            // data passed as text (hopefully)
} PACKET;

struct user
{
    char *badge;            // user's shield number (1M26, Ctrl801, etc etc)
    char *uFN;              // user's first name
    char *uLN;              // user's full last name (we'll truncate it in code)
    char *hireDate;         // date in MMDDYYYY format
    char *division;         // what division are they in (Patrol, Comms, Civ, etc etc)
    bool admin;             // is user admin (true or false)
    bool active;            // is user active (true or false)
    int userlevel;          // user level (1=admin, 2=supervisor, 3=section, 4=user)
    char *modules;          // What modules they have access to (A=Admin, C=CAD, M=MDT, V=Civilian)
};


 
// function protoyping here
void *connection_handler(void *);
void logger(int lvl, char *action);
int construct_record ( char *fn, char *ln, char *dob, char gender, int race );
struct NCIC *db_ncic ( char *fn, char *ln, char *dob );

// functions here

static void printline(const char *errlvl, const char *mesg){
    
    // do date time here instead of in logger
    
    time_t rawtime;
    struct tm * ptm;
    char *date = "";
    char *tyme = "";
    
    time ( &rawtime );
    ptm = gmtime ( &rawtime );
    
    sprintf(date, "%2d%s%4d", ptm->tm_mday, month[(ptm->tm_mon)-1], (ptm->tm_year)+1900);
    sprintf(tyme, "%2d:%2d", ptm->tm_hour, ptm->tm_min);
    
    
    // end date/time stuff
    
    
    if (has_colors() == TRUE) wattron(wout, COLOR_PAIR(DATETIME));
    wprintw(wout, "\n%s %s [", date, time);
    if (has_colors() == TRUE) wattroff(wout, COLOR_PAIR(DATETIME));
    
    if (has_colors() == TRUE) wattron(wout, COLOR_PAIR(ERRLEVEL));
    wprintw(wout, "%s", errlvl);
    if (has_colors() == TRUE) wattroff(wout, COLOR_PAIR(ERRLEVEL));
    
    if (has_colors() == TRUE) wattron(wout, COLOR_PAIR(MSG));
    wprintw(wout, "] %s", mesg);
    if (has_colors() == TRUE) wattroff(wout, COLOR_PAIR(MSG));
}

static void readout(void) {
    wrefresh(wout);
    wrefresh(winp); // leave cursor on input window
}

static void updatewout(void) {
    delwin(wout);
    wout = newwin(winrows - 2, wincols, 0, 0);
    scrollok(wout, true);
    readout();
}

static void updatewinp(void) {
    char *prompt = NULL;
    prompt = "> ";
    delwin(winp);
    winp = newwin(1, wincols, winrows - 2, 0);
    wtimeout(winp,1000); //FIXME not so good idea - input is lost if timeout fires
    if (has_colors() == TRUE) wattron(winp, COLOR_PAIR(WINP)|A_BOLD);
    wprintw(winp, "%s", prompt);
    if (has_colors() == TRUE) wattroff(winp, COLOR_PAIR(WINP)|A_BOLD);
    wrefresh(winp);
}

static void updateall(void) {
    updatewout();
    updatewinp();
}

static void redrawall() {
    getmaxyx(stdscr, winrows, wincols);
    updateall();
    redrawwin(wout);
    redrawwin(winp);
}

static void destroywins(void) {
    delwin(winp);
    delwin(wout);
    endwin();
}




static void sendmesg(const char* mesg) {
    return;  // do nothing here.  merely a place holder for future stuff, maybe?
}

static void readinput(void) {
    // read kybd input
    char *input = "";
    
    int r = wgetnstr(winp, input, LINE_MAX);
    updatewinp();
    
    if (r == KEY_RESIZE) redrawall();
    else if (input == NULL) return;
    else if (strlen(input) == 0) redrawall();
    else if (strcmp(input, EXITPROG) == 0) keepRunning = false;
    else sendmesg(input);
}

static void createwins(void) {
    /* start ncurses mode - do not buffer input */
    initscr();
    cbreak();
    
    /* start color support and setup color pairs */
    if (has_colors() == TRUE) {
        start_color();
        init_pair(DATETIME, COLOR_CYAN, COLOR_BLACK);
        init_pair(ERRLEVEL, COLOR_WHITE, COLOR_BLACK);
        init_pair(MSG, COLOR_CYAN, COLOR_BLACK);
        init_pair(WINP, COLOR_GREEN, COLOR_BLACK);
    }
    
    /* create the windows and add contents */
    redrawall();
}

// Connection handler for each connected client

void *connection_handler(void *socket_desc)
{
    // FUTURE HANDLER -- Handles all client requests
    //Get the socket descriptor
    int sock = *(int*)socket_desc;
    int read_size;
    char *message , client_message[2000];
     
    //Send some messages to the client
    message = "Greetings! I am your connection handler\n";
    write(sock , message , strlen(message));
     
    message = "Now type something and i shall repeat what you type \n";
    write(sock , message , strlen(message));
     
    //Receive a message from client
    while( (read_size = recv(sock , client_message , 2000 , 0)) > 0 )
    {
        //Send the message back to client
        write(sock , client_message , strlen(client_message));
    }
     
    if(read_size == 0)
    {
        puts("Client disconnected");
        fflush(stdout);
    }
    else if(read_size == -1)
    {
        perror("recv failed");
    }
         
    //Free the socket pointer
    free(socket_desc);
     
    return 0;
}

void logger(int lvl, char *action)
{
            
    // assign 
    
    printline(errorlevel[lvl], action);
    
    // printf("[%02d:%02d:%02d] %s - %s\n", (ptm->tm_hour)%24, ptm->tm_min, ptm->tm_sec, user, action);
}


// main program here

void intHandler(int dummy) 
{
    keepRunning = false;
}
 
int main(int argc , char *argv[])
{
    if (setlocale(LC_ALL, "") == NULL)
        logger(2, "Failed to set locale");
    
    int socket_desc , client_sock , c , *new_sock;
    struct sockaddr_in server , client;
     
    //Create socket
    socket_desc = socket(AF_INET , SOCK_STREAM , 0);
    if (socket_desc == -1)
    {
        logger(1, "Could not create socket");
    }
    logger(0, "Socket created");
     
    //Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons( 8888 );
     
    //Bind
    if( bind(socket_desc,(struct sockaddr *)&server , sizeof(server)) < 0)
    {
        //print the error message
        logger(2, "Bind failed. Error");
        return 1;
    }
    logger(0, "Bind done");
     
    //Listen
    listen(socket_desc , 3);
     
    //Accept any incoming connection
    logger(0,"Waiting for incoming connections...");
    c = sizeof(struct sockaddr_in);
    
    // start curses here
    createwins();
    readout();
     
    while( (client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c)) )
    {
        logger(0, "Connection accepted");
         
        pthread_t sniffer_thread;
        new_sock = malloc(1);
        *new_sock = client_sock;
         
        if( pthread_create( &sniffer_thread , NULL ,  connection_handler , (void*) new_sock) < 0)
        {
            logger(2,"Could not create thread.");
            return 1;
        }
         
        //Now join the thread , so that we dont terminate before the thread
        pthread_join( sniffer_thread , NULL);
        logger(0, "Handler assigned");
        
        if(keepRunning == false)
        {
            // someone pressed ctrl-c, so now we shutdown everything and exit with an error
            close(socket_desc);
            return 1;
        }
        
        readinput();
    }
     
    if (client_sock < 0)
    {
        logger(1, "Accept failed.");
        // close socket
        // close (socket_desc);
        destroywins();
        return 1;
    }
     
    return EXIT_SUCCESS;
}
 

