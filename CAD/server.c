/*
    CAD SERVER
    
    Created by: Thomas Kroll aka zhivotnoya.
    
    If you use this code, please credit me as I've done quite a bit of work on it.  Thanks.
    
    Handles connections from the clients, as well as all database calls.  Eventually I'll incorporate
    the actual sqlite calls as well.
    
    Server uses NCURSES so that connections and requests as well as some server functionality can be seen/executed
    on the server itself.
    
    Current server commands will be:
      .quit        - Shuts down server
      .ver         - prints version number
      .uptime      - shows how long server has actually been running
      .res         - shows resources being used by process
      .bcast <msg> - used to broadcast messages to all connected clients.  useful if you need to restart server for some
                     reason.
*/
 
#include <stdio.h>
#include <string.h>          //strlen
#include <stdlib.h>          //strlen
#include <pwd.h>
#include <limits.h>
#include <fcntl.h>
#include <locale.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>       //inet_addr
#include <unistd.h>          //write
// #include <sqlite3.h>         //for local database handling
#include <time.h>            //used for timestamping
#include <signal.h>          //used to catch Ctrl-C and other things
#include <err.h>             //used to handle errors?
#include <errno.h>
#include <ncurses.h>         //so that the server has an interface
#include <pthread.h>

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


// program commands
#define EXITPROG        ".quit"
#define OPTVER          ".ver"
#define UPTIME          ".uptime" 
#define OPTRES          ".res"
#define BCAST           ".bcast"

#define PORT            8888
#define MAXMSG          512

#define NUM_THREADS     5

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

struct log_s
{
    char *errlvl;
    char *mesg;
};


 
// function protoyping here
void *connection_handler(void *);
void logger(int lvl, char *action);
int construct_record ( char *fn, char *ln, char *dob, char gender, int race );
struct NCIC *db_ncic ( char *fn, char *ln, char *dob );

// functions here



int make_socket (uint16_t port){
    int sock;
    struct sockaddr_in name;
    
    /* create the socket */
    sock = socket (PF_INET, SOCK_STREAM, 0);
    if(sock < 0)
    {
        logger(2, "Socket creation failed. Miserably.");
        exit (EXIT_FAILURE);
    }
    
    /* give the socket a name */
    name.sin_family = AF_INET;
    name.sin_port = htons (port);
    name.sin_addr.s_addr = htonl (INADDR_ANY);
    if (bind (sock, (struct sockaddr *) &name, sizeof(name)) < 0)
    {
        logger(2, "Bind error.  Dude, really?");
        exit (EXIT_FAILURE);
    }
    
    return sock;
}

static int read_from_client (int filedes) {
    char buffer[MAXMSG];
    int nbytes;
    
    nbytes= read (filedes, buffer, MAXMSG);
    if (nbytes < 0)
    {
        /* read error */
        char *errmesg = "";
        sprintf(errmesg, "Read error from client(%d).", filedes);
        logger(1, errmesg);
    } 
    else if (nbytes == 0)
    {
        /* end of file */
        return -1;
    }
    
    /* data read */
    // do nothing for now but eventually this will handle client input requests
    return 0;
}

static void *printline(void *loggerdata){
    
    // do date time here instead of in logger
    
    // get our logger data to print out.
    struct log_s *mydata;
    
    time_t rawtime;
    struct tm * ptm;
    
    time ( &rawtime );
    ptm = gmtime ( &rawtime );
    
    // end date/time stuff
    
    if (has_colors() == TRUE) wattron(wout, COLOR_PAIR(MSG));
    wprintw(wout, "\n%2d%s%4d %2d:%2d [", ptm->tm_mday, month[(ptm->tm_mon)-1], (ptm->tm_year)+1900, ptm->tm_hour, ptm->tm_min);
    if (has_colors() == TRUE) wattroff(wout, COLOR_PAIR(MSG));
    
    if (has_colors() == TRUE) wattron(wout, COLOR_PAIR(ERRLEVEL));
    wprintw(wout, "%s", mydata->errlvl);
    if (has_colors() == TRUE) wattroff(wout, COLOR_PAIR(ERRLEVEL));
    
    if (has_colors() == TRUE) wattron(wout, COLOR_PAIR(MSG));
    wprintw(wout, "] %s", mydata->mesg);
    if (has_colors() == TRUE) wattroff(wout, COLOR_PAIR(MSG));
    wrefresh(wout);
    
    return NULL;
    
}

static void readout(void) {
    wrefresh(wout);
    wrefresh(winp); // leave cursor on input window
}

static void updatewout(void) {
    delwin(wout);
    wout = newwin(winrows - 2, wincols, 0, 0);
    scrollok(wout, true);
    wborder(wout, ACS_VLINE, ACS_VLINE, ACS_HLINE, ACS_HLINE, ACS_ULCORNER, ACS_URCORNER, ACS_LLCORNER, ACS_LRCORNER);
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

static void *readinput(void *x_void_ptr) {
    // read kybd input
    char *input = "";
    //if ((input = calloc(1, LINE_MAX)) == NULL)
    //    err(EXIT_FAILURE, "failed to allocate space for input");
    
    int r = wgetnstr(winp, input, 1024);
    updatewinp();
    
    if (r == KEY_RESIZE) redrawall();
    else if (input == NULL) return NULL;
    else if (strlen(input) == 0) redrawall();
    else if (strcmp(input, EXITPROG) == 0) keepRunning = false;
    else sendmesg(input);
    
    return NULL;
}

static void createwins(void) {
    /* start ncurses mode - do not buffer input */
    initscr();
    cbreak();
    nodelay(winp,TRUE);
    
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


void logger(int lvl, char *action)
{
    /* create new thread to print out log stmt */
    pthread_t log_thread;
    int rc;
        
    struct log_s newlog;
    
    newlog.errlvl = errorlevel[lvl];
    newlog.mesg   = action;
    
    rc = pthread_create(&log_thread, NULL, printline, (void *) &newlog);
    
    if (rc) {
        wprintw(wout, "Error: return code from pthread_create() is %d\n", rc);
        exit(-1);
    }
    
    
    
    //printline(errorlevel[lvl], action);
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
    
    // extern int make_socket (uint16_t port);
    int sock;
    fd_set active_fd_set, read_fd_set;
    int i;
    struct sockaddr_in clientname;
    socklen_t size;
    
    // start curses here
    createwins();
    readout();
    
    /* create the socket and set it up to accept connections */
    
    sock = make_socket (PORT);
    if (listen (sock, 1) < 0)
    {
        char *errmesg = "";
        sprintf(errmesg, "Cannot listen on port %d", PORT);
        logger(2,errmesg);
        exit (EXIT_FAILURE);
    }
    
    /* init the set of active sockets */
    FD_ZERO (&active_fd_set);
    FD_SET  (sock, &active_fd_set);
    
    // read input from winp before we enter that loop
    
    int xx = 0;
    pthread_t read_input_thread;
    if(pthread_create(&read_input_thread, NULL, readinput, &xx)) {
        logger(2, "Error creating thread.");
        return 1;
    }
    
    //readinput();
    
    while(keepRunning)
    {
        /* block until input arrives on one or more active sockets */
        read_fd_set = active_fd_set;
        if (select (FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0)
        {
            logger(1, "Socket Error");
        }
        logger(0, "Socket open, listening for connections.");
        /* service all the sockets with input pending */
        for (i = 0; i < FD_SETSIZE; ++i)
            if (FD_ISSET (i, &read_fd_set))
            {
                if (i == sock)
                {
                    /* connection request on original socket */
                    int new;
                    size = sizeof (clientname);
                    new = accept (sock, (struct sockaddr *) &clientname, &size);
                    
                    if (new < 0)
                    {
                        logger(2, "New connection NOT accepted.");
                    }
                    
                    char *contxt = "";
                    sprintf(contxt, "Connect from host %s, port %u", inet_ntoa(clientname.sin_addr), ntohs(clientname.sin_port));
                    logger(0, contxt);
                    FD_SET (new, &active_fd_set);
                    
                }
                else
                {
                    /* data arriving on already-connected socket */
                    if (read_from_client(i)<0)
                    {
                        close(i);
                        FD_CLR(i, &active_fd_set);
                    }
                }
            }
    }
    
    if(pthread_join(read_input_thread, NULL)) {
        logger(1, "Error joining thread.");
        return 2;
    }
    destroywins();
    return EXIT_SUCCESS;
}
 

