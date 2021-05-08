//Hobby creation simple file//

/*
 * Mysle ze lepiej byloby uzyc lepszego parsera i wgl ale na szybko zrobione moze byc noi dane nie sa kopiowane
 * co moze byc tam jakas zaleta. No taki prosty TCP mial byc zeby ogarnac podstawy - to ogarnolem!
 * mysle ze bardziej zaawansowane apki pisac byc moze na podstawie tego pliku i komentarzy w nim zawartych
 * powinienem potrafic
 */

#include <stdio.h>
#include <string.h>
#include <lwip/tcp.h>
#include <tcp_serverStMachine.h>

#include "main.h"   //for HAL functions
#include "gpio.h"

#define ERR_EXIT 100
#define IAC      255    /* Interpret as command. */
#define DONT     254    /* You are not to use option. */
#define DO       253    /* Please, you use option. */
#define WONT     252    /* I won't use option. */
#define WILL     251    /* I will use option. */

static const char invitation[] =      //dopiski const powoduja ze te dane nie som kopiowane do ramu w startupie
  "\r\n"
  "This is TCP server running on top of the lwIP stack.\r\n"
  "Press h or ? to get help.\r\n"
  "\r\n"
  "> ";

static const char help[] =
  "\r\n"
  "h or ? print this help\r\n"
  "G      turn on the green led\r\n"
  "g      turn off the green led\r\n"
  "R      turn on the red led\r\n"
  "r      turn off the red led\r\n"
  "t      get microcontroller running time\r\n"
  "x      exit\r\n"
  "\r\n"
  "> ";

static const char prompt[] =
  "\r\n"
  "> ";

static const char error[] =
  "\r\n"
  "ERROR\r\n"
  "\r\n"
  "> ";

static const char timeout[] =
  "\r\n"
  "TIMEOUT\r\n";

static const char timefrm[] =
  "\r\n"
  "%u %02u:%02u:%02u.%03u\r\n"
  "\r\n"
  "> ";

#define SERVER_TIMEOUT 240
#define POLL_PER_SECOND 2
#define TIME_BUF_SIZE 32

#define tcp_write_from_rom(p, x) tcp_write(p, x, sizeof(x) - 1, 0) //arg okresla zeby nie kopiowac danych -- nie ulegna zmianie.


//Struktura!!!!!! Taka fajna :D serio!  // ale brakuje jeszcze argumentu pbuf i bylaby ideolo chyba :D
struct state {
  err_t (* function)(struct state *, struct tcp_pcb *, uint8_t);
  int timeout;
};
//Funkcje w sumie do obslugi bibiloteki LwIP
static err_t
accept_callback(void *, struct tcp_pcb *, err_t);
static err_t
recv_callback(void *, struct tcp_pcb *, struct pbuf *, err_t);
static err_t
poll_callback(void *, struct tcp_pcb *);
static void
conn_err_callback(void *, err_t);
static void
tcp_exit(struct state *, struct tcp_pcb *);


//Funkcje do obslugi automatu
static err_t
StateAutomaton(struct state *, struct tcp_pcb *, struct pbuf *);
static err_t
InitState(struct state *, struct tcp_pcb *, uint8_t);
static err_t
IACState(struct state *, struct tcp_pcb *, uint8_t);
static err_t
OptionState(struct state *, struct tcp_pcb *, uint8_t);
static err_t
HelpState(struct state *, struct tcp_pcb *, uint8_t);
static err_t
OnGreenLedState(struct state *, struct tcp_pcb *, uint8_t);
static err_t
OffGreenLedState(struct state *, struct tcp_pcb *, uint8_t);
static err_t
OnRedLedState(struct state *, struct tcp_pcb *, uint8_t);
static err_t
OffRedLedState(struct state *, struct tcp_pcb *, uint8_t);
static err_t
GetLocalTimeState(struct state *, struct tcp_pcb *, uint8_t);
static err_t
ExitState(struct state *, struct tcp_pcb *, uint8_t);
static err_t
ErrorState(struct state *, struct tcp_pcb *, uint8_t);

/* Ta funkcja nie jest wołana w kontekście procedury obsługi
   przerwania. */
int TCPserverStart(uint16_t port) {
  struct tcp_pcb *pcb, *listen_pcb;
  err_t err;

  pcb = tcp_new();  //przydziel nowy deskryptor

  if (pcb == NULL)
    return -1;  //jesli sie nie udalo wywal blad

  err = tcp_bind(pcb, IP_ADDR_ANY, port);  // jak sie udalo - zwiaz deskryptor z  wszystkimi adresami IP i portem
  	  	  	  	  	  	  	  	  	  	  	  //Z tym IP_ADDR_ANY to tam mozna ew. doczytac jeszcze np. w API biblioteki

  if (err != ERR_OK) {  //jak cos poszlo nie tak to kasujemy wszystko

    /* Trzeba zwolnić pamięć, poprzednio było tcp_abandon(pcb, 0); */
    tcp_close(pcb);

    return -1; //wywal blad
  }

  /* Między wywołaniem tcp_listen a tcp_accept stos lwIP nie może
     odbierać połączeń. */

  listen_pcb = tcp_listen(pcb);  //rozpocznij nasluchiwanie TCP i usun wskazany deskruptor  typu tcp_pcb
  	  	  	  	  	  	  	  	  //zwraca nowy adres deskryptora nie dokonca wiem jeszcze o co kmn ale tak ma byc
  	  	  	  	  	  	  	  	  	  	  //to sie dzieje w kodzie! :D
    tcp_accept(listen_pcb, accept_callback);  //przekazuje wskaznik do funkcji co sie wywola jak przyjdzie polaczenie


  if (listen_pcb == NULL) {  //jesli sie nie udalo przydzielic niczego

    /* Trzeba zwolnić pamięć, poprzednio było tcp_abandon(pcb, 0); */
    tcp_close(pcb);

    return -1;
  }
  return 0;
}

err_t accept_callback(void *arg, struct tcp_pcb *pcb, err_t err) {
  struct state *state;

  /* Z analizy kodu wynika, że zawsze err == ERR_OK.
  if (err != ERR_OK)
    return err;
  */

  state = mem_malloc(sizeof(struct state));  //alokuje se pamiec do strukturki state (dekl w tym pliku na gorze.. ;) )
  if (state == NULL) //jesli sie nie udalo....
    return ERR_MEM;
  //a jak sie udalo:
  state->function = InitState;   //wpisuje wskaznik do funkcji Init
  state->timeout = SERVER_TIMEOUT;  //ustawiam timeout
  tcp_arg(pcb, state);			//przekazuje ten arg czyli argument do zaalokowanej struktury state w mallocu

  tcp_err(pcb, conn_err_callback);    		// wskaznik do funkcji w przypadku bledu - LwIP to sb wola
  tcp_recv(pcb, recv_callback);				//przekazuje bibliotece co ma wolac jak dostane dane
  tcp_poll(pcb, poll_callback, POLL_PER_SECOND);	//to sie wola po czasie PoolperSec

  return tcp_write_from_rom(pcb, invitation); //wysylam ciag znakow powitania to samo mi wykrywa koniec tablicy
  	  	  	  	  	  	  	  	  	  	  	  //to jest makro zdefiniowane w tym pliku! na gorze..
}

err_t recv_callback(void *arg,
                    struct tcp_pcb *pcb,
                    struct pbuf *p,
                    err_t err) {
  /* Z analizy kodu wynika, że zawsze err == ERR_OK.  //nwm ale tak! :D
  if (err != ERR_OK)
    return err;
  */
  if (p) {  //wskaznik jest rozny wiekszy od 0 wiec prawda.. gdy nic nie ma to jest rowny 0...
    /* Odebrano dane. */
	        tcp_recved(pcb, p->tot_len); //informuje biblioteke o odbiorze danych
    err = StateAutomaton(arg, pcb, p);    //obsluga maszyny stanow!
    if (err == ERR_OK || err == ERR_EXIT)  //jak wyszstko poszlo ok to skaujesz buforki
      pbuf_free(p);   //zwolnij lanuch buforow wskazywany przez p
    if (err == ERR_EXIT) {  //zostala zwrocona komenda zamkniecia z funkcji tam ExitState chyba
      tcp_exit(arg, pcb);
      err = ERR_OK;

//      tcp_recved(pcb, p->tot_len); //informuje biblioteke o odbiorze danych
    }
  }
  else {
    /* Druga strona zamknęła połączenie. */
    tcp_exit(arg, pcb);
    err = ERR_OK;
  }
  return err;
}

err_t poll_callback(void *arg, struct tcp_pcb *pcb) {  //to ten callback wolany co jakis czas
  struct state *state = arg;

  if (state == NULL)   //jesli cos usunelo wskaznik... -- ale zabezpieczen xd
    /* Najwyraźniej połączenie nie zostało jeszcze zamknięte. */
    tcp_exit(arg, pcb);				//wrazie jak cos poszlo nie tak to ma na celu usunac pamiec struktury state to to dobre jest
  else if (--(state->timeout) <= 0) { 			//a jak osiagne timeout to tez tylko dac sygnal klientowi o TIMEOUCie - bo sie obijal! :D
    tcp_write_from_rom(pcb, timeout);
    tcp_exit(arg, pcb);
  }
  return ERR_OK;
}

void conn_err_callback(void *arg, err_t err) {
  mem_free(arg);										//jak cos poszlo nie tak to trzeba zwolnic pamiec wiadomix
}

void tcp_exit(struct state *state, struct tcp_pcb *pcb) {  //ogolnie extra funkcja zabezpieczajaca :)
  /* Po wywołaniu tcp_close() mogą jeszcze przyjść dane, ale nie
     można ich już obsłużyć po wywołaniu mem_free(), a po wywołaniu
     tcp_close() może już nie być szansy wywołania mem_free(). */
  tcp_recv(pcb, NULL);
  /* Ta funkcja może być wywołana wielokrotnie, np. przez
     poll_collback(), jeśli tcp_close() zawiedzie. */
  if (state) {
    mem_free(state);  //poprostu kasujemy wszycko
    tcp_arg(pcb, NULL);  //kasuje wskaznik
  }
  tcp_close(pcb); //zwolnij deskryptor i wgl zamknij polaczenie
}

							//Tutaj obsluga calego automatu stanow!!!! czyli aplikacja uytkownika! ;)
err_t StateAutomaton(struct state *s,
                     struct tcp_pcb *pcb,
                     struct pbuf *p) {
  s->timeout = SERVER_TIMEOUT;
  for (;;) {
    uint8_t *c = (uint8_t *)p->payload;
    uint16_t i;
    err_t err;

    for (i = 0; i < p->len; ++i)
      if (ERR_OK != (err = s->function(s, pcb, c[i])))
        return err;
    if (p->len == p->tot_len)
      break;
    else
      p = p->next;
  }
  return ERR_OK;
}

err_t InitState(struct state *state,
                struct tcp_pcb *pcb,
                uint8_t c) {
  switch (c) {
    case IAC:
      state->function = IACState;
      return ERR_OK;
    case ' ':
    case '\t':
    case '\r':
      return ERR_OK;
    case '\n':
      return tcp_write_from_rom(pcb, prompt);
    case 'h':
    case '?':
      state->function = HelpState;
      return ERR_OK;
    case 'G':
      state->function = OnGreenLedState;
      return ERR_OK;
    case 'g':
      state->function = OffGreenLedState;
      return ERR_OK;
    case 'R':
      state->function = OnRedLedState;
      return ERR_OK;
    case 'r':
      state->function = OffRedLedState;
      return ERR_OK;
    case 't':
      state->function = GetLocalTimeState;
      return ERR_OK;
    case 'x':
      state->function = ExitState;
      return ERR_OK;
    case '$':
      /* Zawieś serwer, żeby przetestować watchdog. */ // nie chce mi sie tego dodawac - proste xd
    //  for(;;);
    default:
      state->function = ErrorState;
      return ERR_OK;
  }
}

static err_t IACState(struct state *state,  //taka w sumie dziwna funkcja xd pomocna do telnetu ignorujemy jeden znak po 0xFF gitara czaje
                      struct tcp_pcb *pcb,
                      uint8_t c) {
  switch (c) {
    case DONT:
    case DO:
    case WONT:
    case WILL:
      state->function = OptionState;			//
      return ERR_OK;
    default:
      state->function = ErrorState;
      return ERR_OK;
  }
}

static err_t OptionState(struct state *state,
                         struct tcp_pcb *pcb,
                         uint8_t c) {
   state->function = InitState;
   return ERR_OK;
}

static err_t HelpState(struct state *state,  //wiadomo drukujemy help state
                       struct tcp_pcb *pcb,
                       uint8_t c) {
  switch (c) {
    case ' ':
    case '\t':
    case '\r':
      return ERR_OK;
    case '\n':
      state->function = InitState;
      return tcp_write_from_rom(pcb, help);
    default:
      state->function = ErrorState;
      return ERR_OK;
  }
}

err_t OnGreenLedState(struct state *state,  //tu migamy diodka
                      struct tcp_pcb *pcb,
                      uint8_t c) {
  switch (c) {
    case ' ':
    case '\t':
    case '\r':
      return ERR_OK;
    case '\n':
      state->function = InitState;
    	HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
      return tcp_write_from_rom(pcb, prompt);
    default:
      state->function = ErrorState;
      return ERR_OK;
  }
}

err_t OffGreenLedState(struct state *state, //tu migamy diodka
                       struct tcp_pcb *pcb,
                       uint8_t c) {
  switch (c) {
    case ' ':
    case '\t':
    case '\r':
      return ERR_OK;
    case '\n':
      state->function = InitState;
   	HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
      return tcp_write_from_rom(pcb, prompt);
    default:
      state->function = ErrorState;
      return ERR_OK;
  }
}

err_t OnRedLedState(struct state *state, //tu migamy diodka
                    struct tcp_pcb *pcb,
                    uint8_t c) {
  switch (c) {
    case ' ':
    case '\t':
    case '\r':
      return ERR_OK;
    case '\n':
      state->function = InitState;
   	HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
      return tcp_write_from_rom(pcb, prompt);
    default:
      state->function = ErrorState;
      return ERR_OK;
  }
}

err_t OffRedLedState(struct state *state, //tu migamy diodka
                     struct tcp_pcb *pcb,
                     uint8_t c) {
  switch (c) {
    case ' ':
    case '\t':
    case '\r':
      return ERR_OK;
    case '\n':
      state->function = InitState;
//      GPIOC->BSRR= //maska pinu na rejestrach jest kude prosciej ja tym halu czasem serio jakby to lepiej ogarnac ae walic
      	  	  	  	  	  //w tym przypadku hal to nawet znosnie robi! :D
 	HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_RESET);
      return tcp_write_from_rom(pcb, prompt);
    default:
      state->function = ErrorState;
      return ERR_OK;
  }
}

void GetLocalTime(unsigned *day,
                  unsigned *hour,
                  unsigned *minute,
                  unsigned *second,
                  unsigned *milisecond) {
  uint32_t t = HAL_GetTick();
  *milisecond = t % 1000;
  t /= 1000;
  *second = t % 60;
  t /= 60;
  *minute = t % 60;
  t /= 60;
  *hour = t % 24;
  t /= 24;
  *day = t;
}



err_t GetLocalTimeState(struct state *state, //tu trzeba zrobic czas od uruchomienia procka :/ - zrobimy
                        struct tcp_pcb *pcb,
                        uint8_t c) {
  switch (c) {
    case ' ':
    case '\t':
    case '\r':
      return ERR_OK;
    case '\n': {
      char buf[TIME_BUF_SIZE];
     unsigned d, h, m, s, ms;
      int size;
      struct _reent reent;

      state->function = InitState;
      GetLocalTime(&d, &h, &m, &s, &ms);
      /* Funkcja snprintf nie jest współużywalna (ang. reentrant).  //chodzi o ew. przerwanie
         Rozwiązanie nie jest przenośne, bo funkcja _snprintf_r nie
         należy do standardowej biblioteki C.
      size = snprintf(buf, TIME_BUF_SIZE, timefrm, d, h, m, s, ms);
      */
      size = _snprintf_r(&reent, buf, TIME_BUF_SIZE, timefrm, d, h, m, s, ms);
      if (size <= 0)
        return tcp_write_from_rom(pcb, error);
      if (size >= TIME_BUF_SIZE)
        /* Nie wysyłaj terminalnego zera. */
        size = TIME_BUF_SIZE - 1;
      return tcp_write(pcb, buf, size, TCP_WRITE_FLAG_COPY);
    }
    default:
      state->function = ErrorState;
      return ERR_OK;
  }
}

err_t ExitState(struct state *state,
                struct tcp_pcb *pcb,
                uint8_t c) {
  switch (c) {
    case ' ':
    case '\t':
    case '\r':
      return ERR_OK;
    case '\n':
      return ERR_EXIT;
    default:
      state->function = ErrorState;
      return ERR_OK;
  }
}

err_t ErrorState(struct state *state,
                 struct tcp_pcb *pcb,
                 uint8_t c) {
  switch (c) {
    case '\n':
      state->function = InitState;

 /*     uint8_t *c = (uint8_t *)p->payload;   //to powinno zadzialac tylko nie mam przekazanego wskaznika do pbuf a nie chce mi sie juz
											//  	tego teraz modyfikowac
      uint16_t i;
      uint8_t a[200];

      for (;;) {
        for (i = 0; i < p->len; ++i)
        {
        	c[i]=a[i];
        }
        if (p->len == p->tot_len)
        	{
        	break;
        	}

        else
        	{
        	p = p->next;
        	}
      }
      tcp_write(pcb, a, sizeof(a) - 1, 0);*/
      tcp_write(pcb, "NapiszPoprKomunikat!", sizeof("NapiszPoprKomunikat!") - 1, TCP_WRITE_FLAG_COPY);

      return tcp_write_from_rom(pcb, error);
    default:
      return ERR_OK;
  }
}
