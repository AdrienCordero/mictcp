#include <mictcp.h>
#include <api/mictcp_core.h>
#include <stdbool.h>

const unsigned long timeout_recv = 100;	//	timeout pour recevoir un message
const unsigned long timeout_connection = 1000;	//	timeout pour recevoir le ACK pour la connection
const unsigned long timeout_fin_connection = 5000;	//	timeout pour fermer la connection si l'ack a été perdu

mic_tcp_sock *tab_socket;
int tab_socket_size = 0;
int num_sequence = 0;
int num_ack = 0;

bool *window;
const int size_window = 100;
const int acceptable_loss = 3;

const int loss = 50;

/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
   int result = -1;
   printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
   result = initialize_components(sm); /* Appel obligatoire */
	if (result == -1)
		return -1;
   set_loss_rate(loss);
   tab_socket_size++;
   if (tab_socket_size == 1)
		tab_socket = malloc(sizeof(mic_tcp_sock));
   else
   		tab_socket = realloc(tab_socket, tab_socket_size * sizeof(mic_tcp_sock));
   mic_tcp_sock sock = {.fd = tab_socket_size - 1, .state = IDLE };
   tab_socket[tab_socket_size-1] = sock;

   // Creation de le fenetre
   window = malloc(size_window * sizeof(int));
   for (int i=0; i < size_window; i++)
		window[i] = true;

   return tab_socket_size-1;
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
   	printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
	if (socket >= tab_socket_size || socket < 0 || tab_socket[socket].state == CLOSED)
   		return -1;
	tab_socket[socket].local_addr = addr;
	return 0;
}

/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr)
{
   	printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
	if (socket >= tab_socket_size || socket < 0 || tab_socket[socket].state == CLOSED)
   		return -1;

	tab_socket[socket].state = IDLE;
	addr = malloc(sizeof(mic_tcp_sock_addr));
	tab_socket[socket].remote_addr = *addr;

	return 0;
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
	if (socket >= tab_socket_size || socket < 0 || tab_socket[socket].state == CLOSED)
   		return -1;

	tab_socket[socket].remote_addr = addr;
	
	//	Envoyer SYN
	mic_tcp_header header = { .source_port = tab_socket[socket].local_addr.port, .dest_port = addr.port, .syn = 1 };
	mic_tcp_payload payload = { "", 0 };
	mic_tcp_pdu pdu = { .header = header, .payload = payload };
	int i = IP_send(pdu, tab_socket[socket].remote_addr.ip_addr);
	if (i == -1){
		printf("Erreur lors de l'etablissement de connexion\n");
		return -1;
	}	
	tab_socket[socket].state = SYN_SENT;

	mic_tcp_pdu *pdu_syn_ack = malloc(sizeof(mic_tcp_pdu));
	IP_recv(pdu_syn_ack, &tab_socket[socket].local_addr.ip_addr, &tab_socket[socket].remote_addr.ip_addr, timeout_connection);
	tab_socket[i].remote_addr.ip_addr = addr.ip_addr;
	if (pdu_syn_ack->header.syn != 1 || pdu_syn_ack->header.ack != 1) {
		printf("le PDU recu n'est pas un syn-ack\n");
		exit(1);
	}

	//	Envoie ACK
	mic_tcp_header header_ack = { .source_port = tab_socket[i].local_addr.port, .dest_port = tab_socket[i].remote_addr.port, .ack = 1};
	mic_tcp_payload payload_ack = { "", 0};
	mic_tcp_pdu pdu_ack = { .header = header_ack, .payload = payload_ack };
	if (IP_send(pdu_ack, tab_socket[i].remote_addr.ip_addr) == -1) {
		printf("Erreur lors de l'etablissement de connexon\n");
		exit(EXIT_FAILURE);
	}
	tab_socket[i].state = ESTABLISHED;
	printf("Connected\n");

	return 0;
}

void update_window(bool value) {
	for (int i=0; i < size_window - 1; i++)
		window[i] = window[i+1];
	window[size_window - 1] = value;
}

bool loss_is_acceptable() {
	int cpt = 0;
	for (int i=0; i < size_window; i++)
		if (!window[i])
			cpt++;
	if (cpt > acceptable_loss)
		return false;
	return true;
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
	
	//	Création PDU
    mic_tcp_header header = {.source_port = tab_socket[mic_sock].local_addr.port,
										.dest_port = tab_socket[mic_sock].remote_addr.port,
										.seq_num = num_sequence,
										.syn = 1};
	 mic_tcp_payload payload = {.data = mesg, .size = mesg_size};
	 mic_tcp_pdu pdu = {.header = header, .payload = payload};
	
	 //	Envoie
	 int i = IP_send(pdu, tab_socket[mic_sock].remote_addr.ip_addr);
	 if (i == -1){
		printf("Erreur lors de l'envoie du message\n");
		return mic_tcp_send(mic_sock, mesg, mesg_size);
	 } 

	 //	Attente ACK
	 mic_tcp_pdu pdu_ack;
	 tab_socket[mic_sock].local_addr.ip_addr.addr_size = 0;
	 tab_socket[mic_sock].remote_addr.ip_addr.addr_size = 0;
	 if (IP_recv(&pdu_ack, &tab_socket[mic_sock].local_addr.ip_addr, &tab_socket[mic_sock].remote_addr.ip_addr, timeout_recv) == -1) {
		update_window(false);
		if (loss_is_acceptable()) {
			printf("perte acceptable, timer\n");
	 		return 0;
		}
		printf("perte pas acceptable, timer\n");
		printf("erreur dans l'envoie du pdu ou la reception de l'ACK\n");
		return mic_tcp_send(mic_sock, mesg, mesg_size);
	 }
	 if (pdu_ack.header.ack != 1 || pdu_ack.header.ack_num != num_sequence) {
		update_window(false);
		if (loss_is_acceptable()) {
			printf("perte acceptable, num_seq\n");
			num_sequence = pdu_ack.header.ack_num;
	 		return 0;
		}
		printf("perte pas acceptable, num_seq\n");
		printf("Le PDU recu n'est pas un ACK, valeur : %s\n", pdu_ack.payload.data);
		return mic_tcp_send(mic_sock, mesg, mesg_size);
	 }

	 //	Reception correct de l'ACK
	 update_window(true);
	 num_sequence++;
	 return i;
}
	
/*
 * Permet à l’application réceptrice de réclamer la récupération d’une donnée
 * stockée dans les buffers de réception du socket
 * Retourne le nombre d’octets lu ou bien -1 en cas d’erreur
 * NB : cette fonction fait appel à la fonction app_buffer_get()
 */
int mic_tcp_recv (int socket, char* mesg, int max_mesg_size)
{
   printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
	if (socket >= tab_socket_size || socket < 0 || tab_socket[socket].state == CLOSED)
   		return -1;
	mic_tcp_payload payload = {.data = mesg, .size = max_mesg_size};
	//printf("mesg : %s\n", mesg);
   	return app_buffer_get(payload);
}

/*
 * Permet de réclamer la destruction d’un socket.
 * Engendre la fermeture de la connexion suivant le modèle de TCP.
 * Retourne 0 si tout se passe bien et -1 en cas d'erreur
 */
int mic_tcp_close (int socket)
{
   	printf("[MIC-TCP] Appel de la fonction :  "); printf(__FUNCTION__); printf("\n");
	if (socket >= tab_socket_size || socket < 0 || tab_socket[socket].state == CLOSED)
   		return -1;

	//	Send FIN
	mic_tcp_header header = {.source_port = tab_socket[socket].local_addr.port, .dest_port = tab_socket[socket].remote_addr.port, .fin = 1};
	mic_tcp_payload payload = { "", 0 };
	mic_tcp_pdu pdu = { .header = header, .payload = payload };
	if (IP_send(pdu, tab_socket[socket].remote_addr.ip_addr) == -1) {
		printf("erreur lors de la fermeture de connexion\n");
		return -1;
	}
	tab_socket[socket].state = CLOSING;

	//	Reception FIN ACK
	mic_tcp_pdu *pdu_fin_ack = malloc(sizeof(mic_tcp_pdu));
	if (IP_recv(pdu_fin_ack, &tab_socket[socket].local_addr.ip_addr, &tab_socket[socket].remote_addr.ip_addr, timeout_fin_connection) == -1)
		return mic_tcp_close(socket);
	if (pdu_fin_ack->header.fin != 1 || pdu_fin_ack->header.ack != 1) {
		printf("le pdu recu n'est pas un fin_ack\n");
		return mic_tcp_close(socket);
	}

	//	Send ACK
	mic_tcp_header header_ack = {.source_port = tab_socket[socket].local_addr.port, .dest_port = tab_socket[socket].remote_addr.port, .ack = 1};
	mic_tcp_payload payload_ack = { "", 0 };
	mic_tcp_pdu pdu_ack = { .header = header_ack, .payload = payload_ack };
	if (IP_send(pdu_ack, tab_socket[socket].remote_addr.ip_addr) == -1) {
		printf("erreur lors de la fermeture de connexion\n");
		return -1;
	}

	tab_socket[socket].state = CLOSED;
	printf("socket ferme\n");

   	return 0;
}

/*
 * Traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis insère les données utiles du PDU dans
 * le buffer de réception du socket. Cette fonction utilise la fonction
 * app_buffer_put().
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_ip_addr local_addr, mic_tcp_ip_addr remote_addr)
{
   	printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
	for (int i=0; i < tab_socket_size; i++) {
		if (tab_socket[i].local_addr.port == pdu.header.dest_port) {

			 if (tab_socket[i].state == IDLE) {
				if (pdu.header.syn != 1) {
					printf("le PDU recu n'est pas un syn\n");
					exit(EXIT_FAILURE);
				}
				tab_socket[i].remote_addr.ip_addr = remote_addr;
				tab_socket[i].remote_addr.port = pdu.header.dest_port;

				//	Envoie SYN-ACK
				mic_tcp_header header = { .source_port = tab_socket[i].local_addr.port, .dest_port = tab_socket[i].remote_addr.port, .ack = 1, .syn = 1};
				mic_tcp_payload payload = { "", 0 };
				mic_tcp_pdu pdu_synack = { .header = header, .payload = payload };
				mic_tcp_ip_addr ip_addr = tab_socket[i].remote_addr.ip_addr;
				int i = IP_send(pdu_synack, ip_addr);
				if (i == -1){
					printf("Erreur lors de l'etablissement de connexion\n");
					exit(EXIT_FAILURE);
				}
				tab_socket[i].state = SYN_RECEIVED;
			}

			else if (tab_socket[i].state == SYN_RECEIVED) {
				if (pdu.header.ack != 1) {
					printf("le PDU recu n'est pas un ack\n");
					exit(1);
				}
				tab_socket[i].state = ESTABLISHED;
				printf("Connected\n");
			}

			else if (tab_socket[i].state == ESTABLISHED) {

				if (pdu.header.fin == 1) {
					printf("fermeture de connexion...\n");
					mic_tcp_header header = {
						.source_port = tab_socket[i].local_addr.port,
						.dest_port = tab_socket[i].remote_addr.port,
						.ack_num = pdu.header.seq_num,
						.ack = 1,
						.fin = 1
					};
					mic_tcp_payload payload = {.data = "", .size = 0};
					mic_tcp_pdu pdu_ack = {.header = header, .payload = payload };
					if (IP_send(pdu_ack, remote_addr) == -1) {
						printf("Erreur dans l'envoie de l'ACK\n");
						i--;
						continue;
					}
					tab_socket[i].state = CLOSING;
				}

				//	Envoie ACK
				mic_tcp_header header = {
					.source_port = tab_socket[i].local_addr.port,
					.dest_port = tab_socket[i].remote_addr.port,
					.ack_num = pdu.header.seq_num,
					.ack = 1
				};
				mic_tcp_payload payload = {.data = "", .size = 0};
				mic_tcp_pdu pdu_ack = {.header = header, .payload = payload };
				if (IP_send(pdu_ack, remote_addr) == -1) {
					printf("Erreur dans l'envoie de l'ACK\n");
					i--;
					continue;
				}

				//	Mise dans le buffer
				if (pdu.header.seq_num == num_ack) {
					app_buffer_put(pdu.payload);
					num_ack++;
				}
				return;
			}

			else if (tab_socket[i].state == CLOSING) {
				tab_socket[i].state = CLOSED;
				printf("socket ferme\n");
				return;
			}
		}
	}
}
