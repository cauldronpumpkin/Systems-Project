#include <iostream>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <time.h>
// #include <bits/stdc++.h>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <pthread.h>
#include <fstream>
#include <cstdint>
#include <semaphore.h>
#include "cards.cpp"

#define MAXREQ 36
#define MAXQUEUE 5
#define MAX_ROOMS 10
using namespace std;

int n;
char reqbuf[MAXREQ];


unordered_map<pthread_t, pthread_cond_t> cond_map; 
pthread_mutex_t mLock = PTHREAD_MUTEX_INITIALIZER; 


unordered_set<int> availableRooms;
unordered_map<int, string> roomNames;
unordered_map<int, unordered_set<string> > roomMembers;

unordered_map<int, unordered_map<string, sem_t*> > roomSemaphores; 

unordered_map<int, string> fd_user_map;
unordered_map<string, int> user_fd_map;
unordered_map<string, int> user_room_map;
unordered_map<thread::id, string> thread_users;
unordered_map<string, thread::id> user_threads;
unordered_set<string> active_users;
unordered_set<string> available_users;

// void sendMessage(int &fd, string s){
// 	char* arr = new char[s.size() + 1];
// 	strcpy(arr, s.c_str());
// 	write(fd, arr, strlen(arr));
// 	delete[] arr;
// }

void sendMsgToUser(string s, string t){
	int fd = user_fd_map[t];
	string sender = thread_users[this_thread::get_id()];

	string res = "FROM " + sender + ": " + s + "\n";
	sendMessage(fd, res);
}

void closeSocket(int &fd){
	string user = fd_user_map[fd];
	active_users.erase(user);
	fd_user_map.erase(fd);
	user_fd_map.erase(user);
	thread_users.erase(this_thread::get_id());
	user_threads.erase(user);
	available_users.erase(user);

	string closingMsg = "Socket has been closed.\n";
	sendMessage(fd, closingMsg);
	close(fd);
}

// room_id => set(usernames);
void playingArea(int room_id){
	unordered_set<string> :: iterator it;
	unordered_map<string, int> player_amount;
	for(it = roomMembers[room_id].begin(); it != roomMembers[room_id].end(); it++)
	{
		int fd = user_fd_map[*it];
		player_amount[*it] = 2000;
		sendMessage(fd, "Welcome to the playing area !!\n");
	}

	int numPlayers = roomMembers[room_id].size();

	while (roomMembers[room_id].size() > 1) {
    
		vector<int> list_fd;
		set<Card*> pack_cards;  // the ones still in pack -> unfolded
		set<Card*> table_cards;  // the ones still in pack -> unfolded
		unordered_map<int, set<Card*> > p_cards;

		for(auto fd : list_fd){
			sendMessage(fd, FLUSH);
			sendMessage(fd, "Now Starting New Game\n");
			this_thread::sleep_for(chrono::milliseconds(1000));
		}

		for(auto it = roomMembers[room_id].begin(); it != roomMembers[room_id].end(); it++){
			set<Card*> temp;
			list_fd.push_back(user_fd_map[*it]);
			p_cards[user_fd_map[*it]] = temp;
		}

		int pot = 100 * roomMembers[room_id].size();
		initPack(pack_cards);
		initPlayers(pack_cards, p_cards, user_fd_map, player_amount, list_fd);
		for (int roundN = 1; roundN < 4; roundN++){
			bool f = play_Round(roundN, pack_cards, table_cards, list_fd, player_amount, fd_user_map, p_cards, pot);
			if (f == 0) // -1 code : only one user on table
				break;
		}

		unordered_set<string> presentPlayers;
		for(auto it = roomMembers[room_id].begin(); it != roomMembers[room_id].end(); it++){
			if (player_amount[*it] > 100){
				presentPlayers.insert(*it);
			}
			else{
				sendMessage(user_fd_map[*it], "You have been eliminated due to insufficient balance! Thanks for playing :)\n");
				// sem_post(roomSemaphores[room_id][*it]);
			}
		}
		roomMembers[room_id] = presentPlayers;
	}

	// sem_post(roomSemaphores[room_id][*(roomMembers[room_id].begin())]);
}
// loop(initPack => initPlayers => blind => flop + play_round1 => turn + play r2 => river + play r3 => judgement)


// 0th client => fdsocket -> send() -> reply() -> 1th client

void waitingArea(int fd, string username, int room_id){
	sendMessage(fd, "You have entered the Waiting Area !!\n");

	sem_t temp_sem;
	sem_init(&temp_sem, 0, 0);

	roomSemaphores[room_id][username] = &temp_sem;

	if (roomMembers[room_id].size() < 2){
		sem_wait(&temp_sem);
	}
	else{
		availableRooms.erase(room_id);
		thread dealer (playingArea, room_id);
		sem_wait(&temp_sem);
	}
}


void joinRoom(int fd, string username, int room_id) {
	user_room_map[username] = room_id;
	roomMembers[room_id].insert(username);

	sendMessage(fd, "Welcome to Room: " + to_string(room_id));

	waitingArea(fd, username, room_id);

	return;
}


void allAvailableRooms(int fd, string username, string instructionMsg){
	string availableRoomNumbers = "Rooms available: \n";

	int i = 0;
	unordered_map<int, int> temp_room_id;
	for (int it : availableRooms){
	    temp_room_id[i] = it;
    	availableRoomNumbers += (to_string(i++) + "  --  " + roomNames[it] + '\n');
	}

	availableRoomNumbers.pop_back();
	sendMessage(fd, availableRoomNumbers + '\n');

	memset(::reqbuf, 0, MAXREQ);
	::n = read(fd, ::reqbuf, MAXREQ-1);

	string msg = string(reqbuf);

	joinRoom(fd, username, temp_room_id[stoi(msg)]);

	return;
}


void createNewRoom(int fd, string username) {
	int room_id = (rand() % MAX_ROOMS);

	sendMessage(fd, "Room Name ?");
	memset(::reqbuf, 0, MAXREQ);
	::n = read(fd, ::reqbuf, MAXREQ-1);

	string msg = string(reqbuf);

	availableRooms.insert(room_id);
	roomNames[room_id] = msg;
	user_room_map[username] = room_id;
	roomMembers[room_id].insert(username);

	waitingArea(fd, username, room_id);

	return;
}


void* serveClient(void* fd) {
	int consockfd = *((int *)fd);


	::n = read(consockfd, ::reqbuf, MAXREQ-1);
	string username = string(::reqbuf) + "::" + to_string(rand() % 1000);

	fd_user_map[consockfd] = username;
	user_fd_map[username] = consockfd;
	active_users.insert(username);
	available_users.insert(username);
	thread_users[this_thread::get_id()] = username;
	user_threads[username] = this_thread::get_id();


	string instructionMsg = "1 Join a Random Room\n2 See all available Rooms\n3 Create a New Room\nq Quit\n";
	string welcomeMsg = "Welcome " + username + " to Arcade Studio\n\n" + instructionMsg;
	sendMessage(consockfd, welcomeMsg);

	while (1)
	{
		memset(::reqbuf, 0, MAXREQ);
		::n = read(consockfd, ::reqbuf, MAXREQ-1);

		string msg = string(reqbuf);

		if(user_room_map.find(username) == user_room_map.end()){
			if (msg == "CLOSE")
			closeSocket(consockfd);
			
			else if (msg == "1"){
			// available_users.erase(username);
			sendMessage(consockfd, "Type an available Room Id or ? for a random room.");
			// joinRoom(consockfd, username);
			}
			
			else if (msg == "2"){
				allAvailableRooms(consockfd, username, instructionMsg);
			}
			
			else if (msg == "3"){
			// available_users.erase(username);
				createNewRoom(consockfd, username);
				sendMessage(consockfd, "Welcome to Room: " + to_string(user_room_map[username]));
			}
			
			else if (msg == "q" || msg == "Q" || msg == "q\n"){
				cout << "clicked";
				closeSocket(consockfd);
				return NULL;
			}
			
			else{
				sendMessage(consockfd, instructionMsg);
			}
		}
		else {
		};
	}

	return NULL;
}

int main() {
	srand(time(NULL));
	int lstnsockfd, consockfd, portno = 9010;
	unsigned int clilen;
	struct sockaddr_in serv_addr, cli_addr;

	memset((char *) &serv_addr,0, sizeof(serv_addr));
	serv_addr.sin_family      = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port        = htons(portno);

	lstnsockfd = socket(AF_INET, SOCK_STREAM, 0);

	bind(lstnsockfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr));
	printf("\nBounded to port\n");

	thread tid[99];
	int tcounter = 0;
	printf("\nListening for incoming connections\n");

	while (1) {
		listen(lstnsockfd, MAXQUEUE); 

		int newsocketfd = accept(lstnsockfd, (struct sockaddr *) &cli_addr, &clilen);

		tid[tcounter] = thread(serveClient, &newsocketfd);

		tcounter++;

		printf("Accepted connection\n");
	}
	close(lstnsockfd);
}
