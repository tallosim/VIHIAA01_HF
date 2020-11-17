#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <wchar.h>
#include <locale.h>

typedef struct Position			//GPS koordin�t�k
{
	double lat;					//sz�less�gi k�r
	double lon;					//hossz�s�gi k�r
} Position;

typedef struct Time				//id�
{
	int h;						//�ra
	int m;						//perc
	int s;						//m�sodperc
}Time;

typedef struct Stop					//�llom�s
{
	char ID[7];						//�llom�s ID-je, 6 karakter
	char name[45];					//�llom�s neve
	Position pos;					//�llom�s GPS koordin�t�i
	struct Stop* nextStop;			//az adatszerkezetben a k�vetkez� elem c�me
	struct Stop* prevStop;			//az algoritmus bej�r�sa alapj�n az ez el�tt belevett elem c�me
	struct Link* prevLink;			//az algoritmus bej�r�sakor az el�z� elemt�l ide vezet� �l
	struct Stop* nextpathStop;		//amikor a megtal�lt cs�csb�l visszamegy�nk a kezdeti c�mhez, r�gz�tj�k az ut�na l�v� c�met a ki�r�shoz
	int shortestTime;				//leghamarabbi id�pillanat amikor eltudjuk �rni
	int IsDone;						//az algoritmus megtal�lta e az adott cs�csot, alapesetben -1
	struct Reach* headReach;		//az adatszerkezetben az �ltala elrhet� cs�csok c�meit tartalmaz� lista kezd�c�me
} Stop;

typedef struct Reach				//el�r�si elem
{
	Stop* way;						//amit el�rek
	int dist;						//ha gyalog el�rhet� a meg�ll�b�l akkor a t�vols�ga, k�l�nben -1
	struct Link* headLink;			//az adatszerkezetben az �ltala elrhet� cs�csokba vezet� �lek c�meit tartalmaz� lista kezd�c�me
	struct Reach* nextReach;		//az adatszerkezetben a k�vetkez� elem c�me
}Reach;

typedef struct Link					//egy �l (j�rat)
{
	int depTime;					//indul�si id�
	int arrTime;					//�rkez�si id�
	char trip_ID[11];				//j�rat azonos�t�ja
	struct Link* nextLink;			//az adatszerkezetben a k�vetkez� elem c�me
}Link;

typedef struct StopTime				//beolvas�shoz haszn�lom hogy vissza tudjam adni az elemeket
{
	char stop_ID[7];
	char trip_ID[11];
	int arrTime;
	int depTime;
	int seq;
}StopTime;

typedef struct Route				//viszonylat
{
	char ID[5];						//viszonylat ID-je, 4 karakter
	char name[5];					//viszonylat neve, max 4 karakter
	int type;						//viszonylat t�pus
	struct Route* nextRoute;		//az adatszerkezetben a k�vetkez� elem c�me
} Route;

typedef struct Trip					//egy j�rat a v�g�llom�st�l v�g�llom�sig
{
	char ID[11];					//�t ID-je, max 10 karakter
	char route_ID[5];				//az adott viszonylat
	char dir[45];					//az j�rat �r�nya
	struct Trip* nextTrip;			//az adatszerkezetben a k�vetkez� elem c�me
} Trip;

/********************************************************************************
*			�tv�ltja a Time struktur�ban t�rolt id�t m�sodpercbe.				*
********************************************************************************/

int GetTimeInSec(Time t)
{
	return t.h * 3600 + t.m * 60 + t.s;
}

/********************************************************************************
*			�tv�ltja a m�sodpercet a Time struktur�ban t�rolt id�re.			*
********************************************************************************/

Time GetTimeinTime(int sec)
{
	Time t;
	t.s = sec % 60;
	t.m = (sec / 60) % 60;
	t.h = sec / 3600;
	return t;
}

/********************************************************************************
*	Beolvas egy sort a meg�ll�kat t�rol� �llom�nyb�l �s elt�rolja egy buffer	*
*	elemben. Ha be tudott olvasni akkor vissza ad 1-et, k�l�nben 0-�t.			*
*	A FILE* fp az �llom�nyra mutat, a Stop* buffer-be olvassa be az adatokat.	*
********************************************************************************/

int readStop(FILE* fp, Stop* buffer)
{
	for (int i = 0; i < 6; i++)
		if (fscanf(fp, "%c", &buffer->ID[i]) != 1)		//ha m�r nem tud beolvasni
			return 0;
	buffer->ID[6] = '\0';
	while (fgetc(fp) != ';');
	char name[45];
	fscanf(fp, "%[^;]", name);
	strcpy(buffer->name, name);
	Position pos;
	while (fgetc(fp) != ';');
	fscanf(fp, "%lf", &pos.lat);
	while (fgetc(fp) != ';');
	fscanf(fp, "%lf", &pos.lon);
	buffer->pos = pos;
	while (fgetc(fp) != '\n');
	return 1;
}

/********************************************************************************
*	Fel�p�ti a l�ncolt-list�t a beolvasott adatokb�l. Addig olvassa be az		*
*	adatokat a readStop() f�ggv�nnyel am�g a f�lj v�g�hez nem �r. V�g�l vissza	*
*	adja a meg�ll�kat t�rol� l�ncolt-lista kezd�c�m�t.							*
********************************************************************************/

Stop* readStops(void)
{
	Stop* headStop = NULL;
	Stop* buff;
	FILE* stops_fp = fopen("stops.txt", "r");
	if (stops_fp == NULL)								//ha nem tudja megny�tni a f�jlt
	{
		printf("\nError! stops.txt not found!\n");
		exit(-1);
		return NULL;
	}
	buff = (Stop*)malloc(sizeof(Stop));
	while (readStop(stops_fp, buff))					//addig olvas ameddig a v�g�ig nem �r a f�ljnak
	{
		buff->headReach = NULL;
		buff->prevStop = NULL;
		buff->prevLink = NULL;
		buff->nextpathStop = NULL;
		buff->shortestTime = -1;
		buff->IsDone = 0;
		buff->nextStop = headStop;
		headStop = buff;
		buff = (Stop*)malloc(sizeof(Stop));
	}
	free(buff);
	fclose(stops_fp);
	printf("Stops are ready!\n");
	return headStop;
}

/********************************************************************************
*	Megkeresi a meg�ll� ID-je alapj�n a megg�ll�t, majd visszaadja a r� mutat�	*
*	pointert. Ha nincs ilyen ID-j� meg�ll� a list�ban visszaad egy NULL ptrt.	*
*	A char ID[7] jel�li a keresend� meg�ll� ID-j�t, a Stop* headStop pedig a	*
*	meg�ll�kat t�rol� l�colt-lista kezd�c�m�re mutat� pointer.					*
********************************************************************************/

Stop* searchStop(char ID[7], Stop* headStop)
{
	for (; headStop != NULL; headStop = headStop->nextStop)
		if (strcmp(ID, headStop->ID) == 0)
			return headStop;							//ha tal�lt olyan meg�ll�t
	return NULL;										//ha nem tal�lt olyan meg�ll�t
}

/********************************************************************************
*	Megkeresi a megg�l� ID-je alapj�n azt a el�rst, ahol a mutatt meg�ll� ID-vel*
*	megegyezik, majd vissza adja a c�m�t. Ha m�g nem l�tezik ilyen el�r�s		*
*	l�trehoz egy �j el�r�st. A char ID[7] jel�li a keresend� meg�ll� ID-j�t, 	*
*	a Stop* headStop pedig a meg�ll�kat t�rol� l�colt-lista kezd�c�m�re mutat�	*
*	pointer, a Stop* stop pedig arra a meg�ll�ra mutat ahol az el�r�sek vanak.	*
********************************************************************************/

Reach* searchReach(char ID[7], Stop* stop, Stop* headStop)
{
	for (Reach* i = stop->headReach; i != NULL; i = i->nextReach)
		if (strcmp(ID, i->way->ID) == 0)
			return i;									//ha tal�t olyan el�r�st
	Reach* reach = (Reach*)malloc(sizeof(Reach));		//ha nem tal�lt olyan el�r�st
	reach->way = searchStop(ID, headStop);
	reach->dist = -1;
	reach->headLink = NULL;
	reach->nextReach = stop->headReach;
	stop->headReach = reach;
	return reach;
}

/********************************************************************************
*	Beolvas egy sort a j�ratok meg�ll�it t�rol� �llom�nyb�l �s elt�rolja egy	*
*	buffer elemben. Ha be tudott olvasni akkor vissza ad 1-et, k�l�nben 0-�t.	*
*	A FILE* fp az �llom�nyra mutat, a StopTime* buffer-be olvassa be az adatokat*
********************************************************************************/

int readStopTime(FILE* fp, StopTime* buffer)
{
	Time arrTime;
	Time depTime;
	for (int i = 0; i < 6; i++)
		if (fscanf(fp, "%c", &buffer->stop_ID[i]) != 1)		//ha m�r nem tud beolvasni
			return 0;
	buffer->stop_ID[6] = '\0';
	while (fgetc(fp) != ';');
	fscanf(fp, "%[^;]", buffer->trip_ID);
	while (fgetc(fp) != ';');
	fscanf(fp, "%d:%d:%d", &arrTime.h, &arrTime.m, &arrTime.s);
	while (fgetc(fp) != ';');
	fscanf(fp, "%d:%d:%d", &depTime.h, &depTime.m, &depTime.s);
	while (fgetc(fp) != ';');
	buffer->arrTime = GetTimeInSec(arrTime);
	buffer->depTime = GetTimeInSec(depTime);
	fscanf(fp, "%d", &buffer->seq);
	while (fgetc(fp) != '\n');
	return 1;
}

/********************************************************************************
*	A beolvasott j�ratok meg�ll�it elt�rolja a f�s�s adatszerkezetben. Addig	*
*	olvassa be az adatokat a readStopTimes() f�ggv�nyel, am�g az �llom�ny v�g�re*
*	nem �r. Az alpj�n, hogy h�nyadik a meg�ll� a j�raton k�l�nb�z� esetekre		*
*	bontja az adatok elt�rol�s�t. Ha 0 akkor m�g nem adja hozz�, hanem csak		*
*	elt�rolja a meg�ll�t �s ut�na beolvas m�g egy sort ahonnan tudja honnan-	*
*	hova megy, �gy le tudja t�rolni. A Stop* headStop a pedig a meg�ll�kat 		*
*	t�rol� l�colt-lista kezd�c�m�re mutat� pointer.								*
********************************************************************************/

void readStopTimes(Stop* headStop)
{
	FILE* stops_times_fp = fopen("stop_times.txt", "r");
	if (stops_times_fp == NULL)								//ha nem tudja megny�tni a f�jlt
	{
		printf("\nError! stop_times.txt not found!\n");
		exit(-1);
		return NULL;
	}
	StopTime* buff = (StopTime*)malloc(sizeof(StopTime));
	Stop* from = NULL;										//melyik meg�ll� el�r�s�be fogja belerakni
	char* trip_ID = (char*)malloc(11 * sizeof(char));
	int depTime;
	while (readStopTime(stops_times_fp, buff))				//addig olvas ameddig a v�g�ig nem �r a f�ljnak
	{
		if (buff->seq == 0)									//ha a j�rat most indul el a v�g�llom�sr�l
		{
			from = searchStop(buff->stop_ID, headStop);
			strcpy(trip_ID, buff->trip_ID);
			depTime = buff->depTime;
		}
		else if (strcmp(buff->trip_ID, trip_ID) == 0)		//ha el�z�leg �s a most beolvasott egy j�rathoz tartozik
		{
			Reach* reach = searchReach(buff->stop_ID, from, headStop);
			Link* link = (Link*)malloc(sizeof(Link));
			link->depTime = depTime;
			link->arrTime = buff->arrTime;
			strcpy(link->trip_ID, buff->trip_ID);
			link->nextLink = reach->headLink;
			reach->headLink = link;
			from = searchStop(buff->stop_ID, headStop);
			depTime = buff->depTime;
		}
		else												//helyes adatok eset�n nem fog ide jutni a program
			printf("\nReading ERROR in stop_times.txt!");
	}
	free(buff);
	fclose(stops_times_fp);
	/*free(trip_ID);*/
	printf("Reaches are ready!\n");
	printf("Links are ready!\n");
}

/********************************************************************************
*	Beolvas egy sort a j�ratokat t�rol� �llom�nyb�l �s elt�rolja egy buffer		*
*	elemben. Ha be tudott olvasni akkor vissza ad 1-et, k�l�nben 0-�t.			*
*	A FILE* fp az �llom�nyra mutat, a Trip* buffer-be olvassa be az adatokat.	*
********************************************************************************/

int readTrip(FILE* fp, Trip* buffer)
{
	for (int i = 0; i < 4; i++)
		if (fscanf(fp, "%c", &buffer->route_ID[i]) != 1)	//ha m�r nem tud beolvasni
			return 0;
	buffer->route_ID[4] = '\0';
	while (fgetc(fp) != ';');
	fscanf(fp, "%[^;]", buffer->ID);
	while (fgetc(fp) != ';');
	fscanf(fp, "%[^\n]", buffer->dir);
	while (fgetc(fp) != '\n');
	return 1;
}

/********************************************************************************
*	Fel�p�ti a l�ncolt-list�t a beolvasott adatokb�l. Addig olvassa be az		*
*	adatokat a readTrip() f�ggv�nnyel am�g a f�lj v�g�hez nem �r. V�g�l vissza	*
*	adja a j�ratokat t�rol� l�ncolt-lista kezd�c�m�t.							*
********************************************************************************/

Trip* readTrips(void)
{
	Trip* headTrip = NULL;
	Trip* buff;
	FILE* trips_fp = fopen("trips.txt", "r");
	if (trips_fp == NULL)								//ha nem tudja megny�tni a f�jlt
	{
		printf("\nError! trips.txt not found!\n");
		exit(-1);
		return NULL;
	}
	buff = (Trip*)malloc(sizeof(Trip));
	while (readTrip(trips_fp, buff))					//addig olvas ameddig a v�g�ig nem �r a f�ljnak
	{
		buff->nextTrip = headTrip;
		headTrip = buff;
		buff = (Trip*)malloc(sizeof(Trip));
	}
	free(buff);
	fclose(trips_fp);
	printf("Trips are ready!\n");
	return headTrip;
}

/********************************************************************************
*	Beolvas egy sort a viszonylatokat t�rol� �llom�nyb�l �s elt�rolja egy buffer*
*	elemben. Ha be tudott olvasni akkor vissza ad 1-et, k�l�nben 0-�t.			*
*	A FILE* fp az �llom�nyra mutat, a Route* buffer-be olvassa be az adatokat.	*
********************************************************************************/

int readRoute(FILE* fp, Route* buffer)
{
	for (int i = 0; i < 4; i++)
		if (fscanf(fp, "%c", &buffer->ID[i]) != 1)	//ha m�r nem tud beolvasni
			return 0;
	buffer->ID[4] = '\0';
	while (fgetc(fp) != ';');
	fscanf(fp, "%[^;]", buffer->name);
	while (fgetc(fp) != ';');
	fscanf(fp, "%d", &buffer->type);
	while (fgetc(fp) != '\n');
	return 1;
}

/********************************************************************************
*	Fel�p�ti a l�ncolt-list�t a beolvasott adatokb�l. Addig olvassa be az		*
*	adatokat a readTrip() f�ggv�nnyel am�g a f�lj v�g�hez nem �r. V�g�l vissza	*
*	adja a viszonylatokat t�rol� l�ncolt-lista kezd�c�m�t.						*
********************************************************************************/

Route* readRoutes(void)
{
	Route* headRoute = NULL;
	Route* buff;
	FILE* routes_fp = fopen("routes.txt", "r");
	if (routes_fp == NULL)								//ha nem tudja megny�tni a f�jlt
	{
		printf("\nError! routes.txt not found!\n");
		exit(-1);
		return NULL;
	}
	buff = (Route*)malloc(sizeof(Route));
	while (readRoute(routes_fp, buff))					//addig olvas ameddig a v�g�ig nem �r a f�ljnak
	{
		buff->nextRoute = headRoute;
		headRoute = buff;
		buff = (Route*)malloc(sizeof(Route));
	}
	free(buff);
	fclose(routes_fp);
	printf("Routes are ready!\n");
	return headRoute;
}

/********************************************************************************
*						�tv�ltja radi�nba a fokot.								*
********************************************************************************/

double DegToRad(double deg)
{
	return deg * (3.14159265358979323846 / 180.0);
}

/********************************************************************************
*	Meghat�rozza k�p meg�ll� t�vols�g�t. Stop* s1, Stop* s2 a k�t meg�ll�.		*
********************************************************************************/

double StopsDist(Stop* s1, Stop* s2)
{
	const int R = 6371000;		//f�ld sugara
	double lat1 = DegToRad(s1->pos.lat), lon1 = DegToRad(s1->pos.lon), lat2 = DegToRad(s2->pos.lat), lon2 = DegToRad(s2->pos.lon);
	return acos(sin(lat1) * sin(lat2) + cos(lat1) * cos(lat2) * cos(lon2 - lon1)) * R;
}

/********************************************************************************
*	Be�ll�tja k�t meg�ll� k�zti t�vols�got. Ha m�r 	Stop* stop el�r�si list�jban*
*	l�tezik Stop* way-be mutat� el�r�s akkor csak a t�vols�got be�ll�tja int	*
*	dist-re. K��nben l�trehoz egy �j el�r�st, amit hozz�ad verem m�dszerrel az	*
*	eddigi el�r�sekhez �s be�ll�tja a t�vols�got.								*
********************************************************************************/

void SetWalkDist(Stop* stop, Stop* way, int dist)
{
	for (Reach* i = stop->headReach; i != NULL; i = i->nextReach)
		if (i->way == way)											//ha m�r szerepel az el�r�sei k�z�tt
		{
			i->dist = dist;
			return;
		}
	Reach* reach = (Reach*)malloc(sizeof(Reach));					//ha m�g nem szerepe az el�resei k�z�tt
	reach->way = way;
	reach->dist = dist;
	reach->headLink = NULL;
	reach->nextReach = stop->headReach;
	stop->headReach = reach;
}

/********************************************************************************
*	Megvizsg�lj minden meg�ll� p�ros�t�st �s kisz�molja a p�rok k�z�tt l�v� 	*
*	t�vols�got, ha ez a �rt�k kisebb mint a int maxDist akkor be�ll�tja a kett�	*
*	t�vols�g�t a SetWalkDist() f�ggv�nnyel. A Stop* headStop pedig a meg�ll�kat	*
*	t�rol� l�colt-lista kezd�c�m�re mutat� pointer.								*
********************************************************************************/

void CreateWalk(Stop* headStop, int maxDist)
{
	for (Stop* i = headStop; i != NULL; i = i->nextStop)
	{
		for (Stop* j = i->nextStop; j != NULL; j = j->nextStop)
		{
			int dist = StopsDist(i, j);
			if (dist <= maxDist)								//ha k�t meg�ll� t�vols�ga a maxDist-en bel�l van
			{
				SetWalkDist(i, j, dist);
				SetWalkDist(j, i, dist);
			}
		}
	}
	printf("Pathways are ready!\n");
}

/********************************************************************************
*	Visszaadja az els� olyan Link-et a Reach* reach link-jei k�z�l, ahol az		*
*	indul�s id�ben k�s�bb van mint, hogy �n oda �rn�k (int time). �s ezek k�z�l	*
*	is a legkor�bbit keresem meg, majd visszaadom azt a Linket.					*
********************************************************************************/

Link* GetTheFirstLink(Reach* reach, int time)
{
	int minTime = 36 * 3600;									//fel�lbecsl�s
	Link* minLink = NULL;
	for (Link* i = reach->headLink; i != NULL; i = i->nextLink)
	{
		if (i->depTime >= time && minTime >= i->arrTime)		//ha meg tudom jav�tani a fel�lbecsl�st
		{
			minTime = i->arrTime;
			minLink = i;
		}
	}
	return minLink;
}

/********************************************************************************
*	V�gigmegy a meg�ll�kon �s megkeresi azt a meg�ll�t amelyett az algoritmus	*
*	m�g nem tett be a k�sz halmazba �s a meg�ll�ba vezet� �t a legr�videbb. Ezt	*
*	a meg�ll�t visszaadja az algoritmus. Ha nincs ilyen akkor NULL-t ad vissza.	*
*	A Stop* headStop a meg�ll�kat �rol� l�colt-lista kezd�c�m�re mutat� pointer.*
********************************************************************************/

Stop* GetMinUnfinshed(Stop* headStop)
{
	int minTime = 100000;																//fel�lbecsl�s
	Stop* minStop = NULL;
	for (Stop* i = headStop; i != NULL; i = i->nextStop)
	{
		if (!i->IsDone && i->shortestTime != -1 && i->shortestTime <= minTime)			//ha meg tudom jav�tani a fel�lbecsl�st
		{
			minTime = i->shortestTime;
			minStop = i;
		}
	}
	return minStop;
}

void DrawLine(FILE* fp, Stop* s1, Stop* s2)
{
	fprintf(fp, "\n{\"type\": \"Feature\", \"properties\": {}, \"geometry\": {\"type\": \"LineString\", \"coordinates\": [[%f, %f], [%f, %f]]}}", s1->pos.lon, s1->pos.lat, s2->pos.lon, s2->pos.lat);
}

void DrawLines(FILE* fp, Stop* headStop)
{
	for (Stop* i = headStop; i != NULL; i = i->nextStop)
	{
		if (i->prevStop != NULL)
		{
			DrawLine(fp, i, i->prevStop);
			if (i->nextStop != NULL)
			{
				fprintf(fp, ",");
			}
		}
	}
		
}

/********************************************************************************
*	A legr�videbb uatkat keres� algoritmus, amely �lmenti jav�t�sokkal dolgozik.*
*	Kezdetben csak a Stop* startStop van az el�rtek k�z�tt, az indul�si id�vel.	*
*	Ut�na az aku�lis meg�ll�b�l el�rhet� meg�ll�kat �lmenti jav�t�ssokkal		*
*	jav�tunk az el�r�si idejeiken. Ezut�n megkeress�k az el nem �rt meg�ll�k	*
*	k�z�l a legkisebb el�r�si idej�t a GetMinUnfinshed() f�ggv�nnyel.			*
*	A Stop* headStop a meg�ll�kat t�rol� l�colt-lista kezd�c�m�re mutat� pointer*
*	A int startTime az indul�si id�, a double walk_speed a gyalogl�s sebbes�ge.	*
********************************************************************************/

void Dijkstra(Stop* headStop, Stop* startStop, int startTime, double walk_speed)
{
	int IsDone = 0;
	startStop->shortestTime = startTime;
	Stop* current = GetMinUnfinshed(headStop);
	if (current == NULL)
		IsDone = 1;
	else
		current->IsDone = 1;
	while (!IsDone)																						//addig fut ameddig nem lehet t�bb �lmenti jav�t�st v�gezni
	{
		for (Reach* i = current->headReach; i != NULL; i = i->nextReach)
		{
			Link* firstLink = GetTheFirstLink(i, current->shortestTime);
			if (firstLink != NULL && i->dist == -1)														//1. eset: az adott el�r�st csak jerm�vel lehet megtenni
			{
				if (firstLink->arrTime <= i->way->shortestTime || i->way->shortestTime == -1)
				{
					i->way->shortestTime = firstLink->arrTime;
					i->way->prevStop = current;
					i->way->prevLink = firstLink;
				}
			}
			else if (firstLink != NULL && i->dist != -1)													//2. eset: az adott el�r�st j�rm�vel �s gyalog is meg lehet tenni
			{
				if (firstLink->arrTime <= (int)(i->dist / walk_speed) + current->shortestTime)			//2/a. eset: ha a j�rm�vel gyorsabban eljutunk az egyik meg�ll�b�l a m�sikba
				{
					if (firstLink->arrTime <= i->way->shortestTime || i->way->shortestTime == -1)
					{
						i->way->shortestTime = firstLink->arrTime;
						i->way->prevStop = current;
						i->way->prevLink = firstLink;
					}
				}
				else if (firstLink->arrTime > (int)(i->dist / walk_speed) + current->shortestTime)		//2/b. eset: gyalog gyorsabb megtenni a t�vot
				{
					if ((int)(i->dist / walk_speed) + current->shortestTime <= i->way->shortestTime || i->way->shortestTime == -1)
					{
						i->way->shortestTime = (int)(i->dist / walk_speed) + current->shortestTime;
						i->way->prevStop = current;
						Link* prevLink = (Link*)malloc(sizeof(Link));
						prevLink->nextLink = NULL;
						strcpy(prevLink->trip_ID, "0000000000");
						prevLink->depTime = current->shortestTime;
						prevLink->arrTime = (int)(i->dist / walk_speed) + current->shortestTime;
						i->way->prevLink = prevLink;
					}
				}
			}
			else if (firstLink == NULL && i->dist != -1)												//3. eset: az adott el�r�st csak gyalog lehet megtenni
			{
				if ((int)(i->dist / walk_speed) + current->shortestTime < i->way->shortestTime || i->way->shortestTime == -1)
				{
					i->way->shortestTime = (int)(i->dist / walk_speed) + current->shortestTime;
					i->way->prevStop = current;
					Link* prevLink = (Link*)malloc(sizeof(Link));
					prevLink->nextLink = NULL;
					strcpy(prevLink->trip_ID, "0000000000");
					prevLink->depTime = current->shortestTime;
					prevLink->arrTime = (int)(i->dist / walk_speed) + current->shortestTime;
					i->way->prevLink = prevLink;
				}
			}
			else
			{
				//seemi se t�rt�nik
			}
		}
		current = GetMinUnfinshed(headStop);		//k�vetkez� aktu�lis meg�ll� a minim�lis el�r�s� lesz
		if (current == NULL)						//ha m�r nem tudok jav�tani
			IsDone = 1;
		else
			current->IsDone = 1;
	}
}

/********************************************************************************
*		K�ir a Stop* stop meg�ll� ID-j�t, el�r�si idej�t, �s nev�t.				*
********************************************************************************/

void printStop(Stop* stop)
{
	Time t = GetTimeinTime(stop->shortestTime);
	if (t.s >= 30)
		t.m++;
	if (t.s != -1)
		printf("\n%02d:%02d #%s %s", t.h, t.m, stop->ID, stop->name);
}

/********************************************************************************
*	A v�gpont�l kezdve vissza megy a kezd� cs�cshoz �s k�zben elt�rolja, hogy	*
*	az aktu�lis meg�ll�b�l melyik a k�vetkez� meg�ll�. A Stop* endStop a v�gpont*
********************************************************************************/

void SetPathNextStops(Stop* endStop)
{
	Stop* before = endStop;
	for (Stop* i = endStop->prevStop; i != NULL; i = i->prevStop)
	{
		i->nextpathStop = before;		//itt �ll�tja be, hogy melyik a k�vetkez� meg�ll� az �tvonalon
		before = i;
	}
}

/********************************************************************************
*	Megkeresi a j�rat ID-je alapj�n a j�ratot, majd visszaadja a r� mutat�		*
*	pointert. Ha nincs ilyen ID-j� j�rat a list�ban visszaad egy NULL pointert.	*
*	A char ID[11] jel�li a keresend� j�rat ID-j�t, a Trip* headTrip pedig a		*
*	j�ratokat t�rol� l�colt-lista kezd�c�m�re mutat� pointer.					*
********************************************************************************/

Trip* searchTrip(char ID[11], Trip* headTrip)
{
	for (; headTrip != NULL; headTrip = headTrip->nextTrip)
		if (strcmp(ID, headTrip->ID) == 0)
			return headTrip;
	return NULL;
}

/********************************************************************************
*	Megkeresi a viszonylat ID-je alapj�n a viszonylatot, majd visszaadja a r� 	*
*	mutat� pointert. Ha nincs ilyen ID-j� viszonylat a list�ban visszaad egy 	*
*	NULL pointert. A char ID[5] jel�li a keresend� viszonylat ID-j�t, a Route*	*
*	headRoute pedig a viszonylatokat t�rol� l�colt-lista kezd�c�m�re mutat� pntr*
********************************************************************************/

Route* searchRoute(char ID[5], Route* headRoute)
{
	for (; headRoute != NULL; headRoute = headRoute->nextRoute)
		if (strcmp(ID, headRoute->ID) == 0)
			return headRoute;
	return NULL;
}

/********************************************************************************
*	Ki�rja a megtal�lt �tvonalat, ahol k�l�n ki�rja a j�rattal val� utaz�st �s	*
*	a gyalogl�st. A Stop* headStop a meg�ll�kat t�rol� l�colt-lista kezd�c�m�re	*
*	mutat� pointer. A Stop* startStop �s a Stop* endStop a k�t v�gpont mutat�ja.*
*	A Route* headRoute pedig a viszonylatokat t�rol� l�colt-lista kezd�c�m�re	*
*	mutat� pointer. A Trip* headTrip pedig a j�ratokat t�rol� l�colt-lista		*
*	kezd�c�m�re mutat� pointer.													*
********************************************************************************/

void PrintPath(Stop* startStop, Stop* endStop, Trip* headTrip, Route* headRoute, double walk_speed)
{
	printf("\n------------------------------------------------------------------------------------------------------------------------");
	SetPathNextStops(endStop);
	printStop(startStop);
	int n = 0;
	int time = startStop->shortestTime;
	int dif_dist = 0;
	for (Stop* i = startStop->nextpathStop; i != NULL; i = i->nextpathStop)
	{
		if (i->nextpathStop == NULL || strcmp(i->prevLink->trip_ID, i->nextpathStop->prevLink->trip_ID) != 0)
		{
			if (strcmp(i->prevLink->trip_ID, "0000000000") != 0)								//1. eset: egy j�raton val� utaz�st �rja ki
			{
				Trip* trip = searchTrip(i->prevLink->trip_ID, headTrip);
				Route* route = searchRoute(trip->route_ID, headRoute);
				printf("\n\t    \t|");
				printf("\n\t    \t|");
				printf("\n\t%4s\tO\t%d stops, Direction: %s", route->name, n + 1, trip->dir);
				printf("\n\t    \t|");
				printf("\n\t    \t|");
				printStop(i);
				time = i->shortestTime;
				n = 0;
			}
			else if (i->nextpathStop != NULL)													//2a. eset: egy gyalogl�st �r ki, amely nem a c�l pozici�hoz vezet
			{
				Time t = GetTimeinTime(i->nextpathStop->prevLink->depTime);
				if (t.s >= 30)
					t.m++;
				dif_dist += walk_speed * (i->shortestTime - time);
				printf("\n\t    \t|");
				printf("\n\t    \tO\t%dm walk", dif_dist);
				printf("\n\t    \t|");
				printf("\n%02d:%02d #%s %s", t.h, t.m, i->ID, i->name);
				dif_dist = 0;
			}
			else																				//2b. eset: egy gyalogl�st �r ki, amely a c�l pozici�hoz vezet
			{
				Time t = GetTimeinTime(i->shortestTime);
				if (t.s >= 30)
					t.m++;
				dif_dist += walk_speed * (i->shortestTime - time);
				printf("\n\t    \t|");
				printf("\n\t    \tO\t%dm walk", dif_dist);
				printf("\n\t    \t|");
				printf("\n%02d:%02d #%s %s", t.h, t.m, i->ID, i->name);
				dif_dist = 0;
			}
		}
		else if (strcmp(i->prevLink->trip_ID, "0000000000") == 0 && strcmp("0000000000", i->nextpathStop->prevLink->trip_ID) == 0)		//ha t�bb gyalogl�s van egym�s ut�n
		{
			dif_dist += walk_speed * (i->shortestTime - time);
		}
		else																															//ha t�bb meg�ll�t utazunk egy j�rattal
			n++;
	}
	printf("\n------------------------------------------------------------------------------------------------------------------------\n");
}

/********************************************************************************
*	T�rli a Stop* headStop-ban t�rolt Stop*-ok a bej�r�sra vonatkoz� adatokat.	*
********************************************************************************/

void FreeDijkstra(Stop* headStop)
{
	for (Stop* i = headStop; i != NULL; i = i->nextStop)
	{
		i->nextpathStop = NULL;
		i->prevStop = NULL;
		if (i->prevLink != NULL)
			if (strcmp(i->prevLink->trip_ID, "0000000000") == 0)
				free(i->prevLink);
		i->prevLink = NULL;
		i->IsDone = 0;
		i->shortestTime = -1;
	}
}

/********************************************************************************
*	Hozz�adja a Stop* headStop l�ncolt-list�hoz verem m�dszerrel a Stop* stop	*
*	meg�ll�t, majd visszaadja az �j l�ncolt-list�ra kezd�c�m�re mutat�j�t.		*
********************************************************************************/

Stop* AddPoint(Stop* headStop, Stop* stop)
{
	stop->headReach = NULL;
	stop->prevStop = NULL;
	stop->prevLink = NULL;
	stop->nextpathStop = NULL;
	stop->shortestTime = -1;
	stop->IsDone = 0;
	stop->nextStop = headStop;
	return stop;
}

/********************************************************************************
*					K�rja a lent lthat� s�m�t (BKK Futar).						*
********************************************************************************/

void PrintLogo(void)
{
	printf("\t\t\t\t ____  _  ___  __     ______     _             \n");
	printf("\t\t\t\t|  _ \\| |/ / |/ /    |  ____|   | |            \n");
	printf("\t\t\t\t| |_) | ' /| ' /     | |__ _   _| |_ __ _ _ __ \n");
	printf("\t\t\t\t|  _ <|  < |  <      |  __| | | | __/ _` | '__|\n");
	printf("\t\t\t\t| |_) | . \\| . \\     | |  | |_| | || (_| | |   \n");
	printf("\t\t\t\t|____/|_|\\_\\_|\\_\\    |_|   \\__,_|\\__\\__,_|_|   \n");
}

/********************************************************************************
*					K�rja a lent lthat� s�m�t (Path not found!).				*
********************************************************************************/

void PrintNotFound(void)
{
	printf("\t\t\t\t\t _____________________________\n");
	printf("\t\t\t\t\t|                             |\n");
	printf("\t\t\t\t\t|       Path not found!       |\n");
	printf("\t\t\t\t\t|_____________________________|\n");
}

/********************************************************************************
*	Bek�ri a felhaszn�l�t�l az indul�si id�t, valamint a kezd� �s v�gpontok GPS	*
*	koordin�t�j�t. A megadott kezd�- �s v�gpont pointereiben el is t�rolja.		*
********************************************************************************/

void Input(Stop* startStop, Stop* endStop, Time* startTime)
{
	system("cls");
	PrintLogo();
	printf("\nPlease enter the start time (hh:mm): ");
	scanf("%d:%d:%d", &startTime->h, &startTime->m);
	startTime->s = 0;
	printf("\nPlease enter the start position!");
	printf("\nLatitude: ");
	scanf("%lf", &startStop->pos.lat);
	printf("Longitude: ");
	scanf("%lf", &startStop->pos.lon);
	strcpy(startStop->ID, "000000");
	strcpy(startStop->name, "Start position");
	printf("\nPlease enter the end position!");
	printf("\nLatitude: ");
	scanf("%lf", &endStop->pos.lat);
	printf("Longitude: ");
	scanf("%lf", &endStop->pos.lon);
	strcpy(endStop->ID, "999999");
	strcpy(endStop->name, "End position");
}

/********************************************************************************
*	Megvizsg�lj a kezd� �s v�gpontok k�zel�ben l�v� meg�ll�kat �s kisz�molja a 	*
*	t�vols�gukat, ha az �rt�k kisebb mint a int maxDist akkor be�ll�tja a k�t	*
*	meg�ll� t�vols�g�t a SetWalkDist() f�ggv�nnyel. A Stop* headStop pedig a 	*
*	meg�ll�kat t�rol� l�colt-lista kezd�c�m�re mutat� pointer.					*
********************************************************************************/

void CreateStartEndWalk(Stop* headStop, int maxDist)
{
	int count = 0;
	for (Stop* i = headStop; count < 2; i = i->nextStop, count++)
	{
		for (Stop* j = i->nextStop; j != NULL; j = j->nextStop)
		{
			int dist = StopsDist(i, j);
			if (dist <= maxDist)							//ha a k�t meg�ll� t�vols�g legfeljebb int maxDist
			{
				SetWalkDist(i, j, dist);
				SetWalkDist(j, i, dist);
			}
		}
	}
}

/********************************************************************************
*		Meg�rdezi a felhaszn�l�t�l, hogy szertne-e �j keres�st ind�tani.		*
********************************************************************************/

int ISNewSearch(void)
{
	char answer;
	printf("\nWould you like a new search? (Y/N)\n");
	while (getchar() != '\n');
	scanf("%c", &answer);
	return answer == 'Y';
}

/********************************************************************************
*		Felszabad�tja a Reach* reach-ben t�rolt Link*-ek mem�ri�j�t.			*
********************************************************************************/

void FreeLink(Reach* reach)
{
	Link* link;
	while (reach->headLink != NULL)
	{
		link = reach->headLink;
		reach->headLink = link->nextLink;
		free(link);
	}
}

/********************************************************************************
*		Felszabad�tja a Stop* stop-ban t�rolt Reach*-ek mem�ri�j�t.				*
********************************************************************************/

void FreeReachs(Stop* stop)
{
	Reach* reach;
	while (stop->headReach != NULL)
	{
		reach = stop->headReach;
		stop->headReach = reach->nextReach;
		FreeLink(reach);
		free(reach);
	}
}

/********************************************************************************
*		Felszabad�tja a Stop* headStop-ban t�rolt Stop*-ok mem�ri�j�t.			*
********************************************************************************/

void FreeStops(Stop* headStop)
{
	Stop* stop;
	while (headStop != NULL)
	{
		stop = headStop;
		headStop = stop->nextStop;
		FreeReachs(stop);
		free(stop);
	}
}

/********************************************************************************
*		Felszabad�tja a Trip* headTrip-ben t�rolt Trip*-ek mem�ri�j�t.			*
********************************************************************************/

void FreeTrips(Trip* headTrip)
{
	Trip* trip;
	while (headTrip != NULL)
	{
		trip = headTrip;
		headTrip = trip->nextTrip;
		free(trip);
	}
}

/********************************************************************************
*		Felszabad�tja a Route* headRoute-ban t�rolt Route*-ok mem�ri�j�t.		*
********************************************************************************/

void FreeRoutes(Route* headRoute)
{
	Route* route;
	while (headRoute != NULL)
	{
		route = headRoute;
		headRoute = route->nextRoute;
		free(route);
	}
}

int main(void)
{
	setlocale(LC_ALL, "");										//be�ll�tja hogy magyar karaktereket is ki lehessen �rni
	double walk_speed = 1.4;													//gyalogl�s sebess�ge
	int max_walk_dist = 500;													//gyalog megtehet� maxim�lis t�vols�g k�t meg�ll� k�z�tt
	int new_search = 1;
	Stop* headStop = readStops();												//beolvassa a meg�ll�kat
	readStopTimes(headStop);													//beolvassa a j�ratok meg�ll�it
	Trip* headTrip = readTrips();												//beolvassa a j�ratokat
	Route* headRoute = readRoutes();											//beolvassa a viszonylatokat
	CreateWalk(headStop, max_walk_dist);										//l�tre hozza a gyalogos kapcsolatokat

	Stop* startStop = NULL, *endStop = NULL;
	Time startTime;

	while (new_search)															//annyiszor keres �tvonalat a felhaszn�l� ah�nyszor csak akar
	{
		startStop = (Stop*)malloc(sizeof(Stop));
		endStop = (Stop*)malloc(sizeof(Stop));

		Input(startStop, endStop, &startTime);									//bek�rj�k a felhaszn�l�t�l az indul�si id�t, valamint a kezd� �s v�gpontot

		headStop = AddPoint(headStop, startStop);								//hozz�adjuk a kezd�pontot
		headStop = AddPoint(headStop, endStop);									//hozz�adjuk a v�gpontot

		CreateStartEndWalk(headStop, max_walk_dist);							//a kezd� �s v�gpont k�r�li gyalog el�rhet� cs�csok kapcsolat�t be�ll�tjuk

		Dijkstra(headStop, startStop, GetTimeInSec(startTime), walk_speed);		//a keres� algoritmus fut�sa

		FILE* fp = fopen("outputs.txt", "w");
		DrawLines(fp, headStop);
		fclose(fp);

		if (endStop->IsDone == 0)												//ha nem tal�lt a kezd� �s v�gpont k�z�tt �tvonalat
			PrintNotFound();
		else																	//ha tal�lt a kezd� �s v�gpont k�z�tt �tvonalat
			PrintPath(startStop, endStop, headTrip, headRoute, walk_speed);		//ki�rja a tal�lt �tvonalat

		FreeDijkstra(headStop);													//t�rli a bej�r�sra vonatkoz� adatokat

		new_search = ISNewSearch();												//megk�rdezi a felhaszn�l�t�l hogy szeretne-e �j keres�st
	}

	FreeStops(headStop);														//felszabad�tja a f� f�s�s adatszerkezetett
	FreeTrips(headTrip);														//felszabad�tja a j�ratokat t�rol� l�ncolt-list�t
	FreeRoutes(headRoute);														//felszabad�tja a viszonylatokat t�rol� l�ncolt-list�t
	return 0;
}