#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <wchar.h>
#include <locale.h>

typedef struct Position			//GPS koordináták
{
	double lat;					//szélességi kör
	double lon;					//hosszúsági kör
} Position;

typedef struct Time				//idõ
{
	int h;						//óra
	int m;						//perc
	int s;						//másodperc
}Time;

typedef struct Stop					//állomás
{
	char ID[7];						//állomás ID-je, 6 karakter
	char name[45];					//állomás neve
	Position pos;					//állomás GPS koordinátái
	struct Stop* nextStop;			//az adatszerkezetben a következõ elem címe
	struct Stop* prevStop;			//az algoritmus bejárása alapján az ez elõtt belevett elem címe
	struct Link* prevLink;			//az algoritmus bejárásakor az elõzõ elemtõl ide vezetõ él
	struct Stop* nextpathStop;		//amikor a megtalált csúcsból visszamegyünk a kezdeti címhez, rögzítjük az utána lévõ címet a kiíráshoz
	int shortestTime;				//leghamarabbi idõpillanat amikor eltudjuk érni
	int IsDone;						//az algoritmus megtalálta e az adott csúcsot, alapesetben -1
	struct Reach* headReach;		//az adatszerkezetben az általa elrhetõ csúcsok címeit tartalmazó lista kezdõcíme
} Stop;

typedef struct Reach				//elérési elem
{
	Stop* way;						//amit elérek
	int dist;						//ha gyalog elérhetõ a megállóból akkor a távolsága, különben -1
	struct Link* headLink;			//az adatszerkezetben az általa elrhetõ csúcsokba vezetõ élek címeit tartalmazó lista kezdõcíme
	struct Reach* nextReach;		//az adatszerkezetben a következõ elem címe
}Reach;

typedef struct Link					//egy él (járat)
{
	int depTime;					//indulási idõ
	int arrTime;					//érkezési idõ
	char trip_ID[11];				//járat azonosítója
	struct Link* nextLink;			//az adatszerkezetben a következõ elem címe
}Link;

typedef struct StopTime				//beolvasáshoz használom hogy vissza tudjam adni az elemeket
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
	int type;						//viszonylat típus
	struct Route* nextRoute;		//az adatszerkezetben a következõ elem címe
} Route;

typedef struct Trip					//egy járat a végállomástól végállomásig
{
	char ID[11];					//út ID-je, max 10 karakter
	char route_ID[5];				//az adott viszonylat
	char dir[45];					//az járat íránya
	struct Trip* nextTrip;			//az adatszerkezetben a következõ elem címe
} Trip;

/********************************************************************************
*			Átváltja a Time strukturában tárolt idõt másodpercbe.				*
********************************************************************************/

int GetTimeInSec(Time t)
{
	return t.h * 3600 + t.m * 60 + t.s;
}

/********************************************************************************
*			Átváltja a másodpercet a Time strukturában tárolt idõre.			*
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
*	Beolvas egy sort a megállókat tároló állományból és eltárolja egy buffer	*
*	elemben. Ha be tudott olvasni akkor vissza ad 1-et, különben 0-át.			*
*	A FILE* fp az állományra mutat, a Stop* buffer-be olvassa be az adatokat.	*
********************************************************************************/

int readStop(FILE* fp, Stop* buffer)
{
	for (int i = 0; i < 6; i++)
		if (fscanf(fp, "%c", &buffer->ID[i]) != 1)		//ha már nem tud beolvasni
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
*	Felépíti a láncolt-listát a beolvasott adatokból. Addig olvassa be az		*
*	adatokat a readStop() függvénnyel amíg a fálj végéhez nem ér. Végül vissza	*
*	adja a megállókat tároló láncolt-lista kezdõcímét.							*
********************************************************************************/

Stop* readStops(void)
{
	Stop* headStop = NULL;
	Stop* buff;
	FILE* stops_fp = fopen("stops.txt", "r");
	if (stops_fp == NULL)								//ha nem tudja megnyítni a fájlt
	{
		printf("\nError! stops.txt not found!\n");
		exit(-1);
		return NULL;
	}
	buff = (Stop*)malloc(sizeof(Stop));
	while (readStop(stops_fp, buff))					//addig olvas ameddig a végéig nem ér a fáljnak
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
*	Megkeresi a megálló ID-je alapján a meggállót, majd visszaadja a rá mutató	*
*	pointert. Ha nincs ilyen ID-jû megálló a listában visszaad egy NULL ptrt.	*
*	A char ID[7] jelöli a keresendõ megálló ID-jét, a Stop* headStop pedig a	*
*	megállókat tároló lácolt-lista kezdõcímére mutató pointer.					*
********************************************************************************/

Stop* searchStop(char ID[7], Stop* headStop)
{
	for (; headStop != NULL; headStop = headStop->nextStop)
		if (strcmp(ID, headStop->ID) == 0)
			return headStop;							//ha talált olyan megállót
	return NULL;										//ha nem talált olyan megállót
}

/********************************************************************************
*	Megkeresi a meggáló ID-je alapján azt a elérst, ahol a mutatt megálló ID-vel*
*	megegyezik, majd vissza adja a címét. Ha még nem létezik ilyen elérés		*
*	létrehoz egy új elérést. A char ID[7] jelöli a keresendõ megálló ID-jét, 	*
*	a Stop* headStop pedig a megállókat tároló lácolt-lista kezdõcímére mutató	*
*	pointer, a Stop* stop pedig arra a megállóra mutat ahol az elérések vanak.	*
********************************************************************************/

Reach* searchReach(char ID[7], Stop* stop, Stop* headStop)
{
	for (Reach* i = stop->headReach; i != NULL; i = i->nextReach)
		if (strcmp(ID, i->way->ID) == 0)
			return i;									//ha talát olyan elérést
	Reach* reach = (Reach*)malloc(sizeof(Reach));		//ha nem talált olyan elérést
	reach->way = searchStop(ID, headStop);
	reach->dist = -1;
	reach->headLink = NULL;
	reach->nextReach = stop->headReach;
	stop->headReach = reach;
	return reach;
}

/********************************************************************************
*	Beolvas egy sort a járatok megállóit tároló állományból és eltárolja egy	*
*	buffer elemben. Ha be tudott olvasni akkor vissza ad 1-et, különben 0-át.	*
*	A FILE* fp az állományra mutat, a StopTime* buffer-be olvassa be az adatokat*
********************************************************************************/

int readStopTime(FILE* fp, StopTime* buffer)
{
	Time arrTime;
	Time depTime;
	for (int i = 0; i < 6; i++)
		if (fscanf(fp, "%c", &buffer->stop_ID[i]) != 1)		//ha már nem tud beolvasni
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
*	A beolvasott járatok megállóit eltárolja a fésüs adatszerkezetben. Addig	*
*	olvassa be az adatokat a readStopTimes() függvényel, amíg az állomány végére*
*	nem ér. Az alpján, hogy hányadik a megálló a járaton különbözõ esetekre		*
*	bontja az adatok eltárolását. Ha 0 akkor még nem adja hozzá, hanem csak		*
*	eltárolja a megállót és utána beolvas még egy sort ahonnan tudja honnan-	*
*	hova megy, így le tudja tárolni. A Stop* headStop a pedig a megállókat 		*
*	tároló lácolt-lista kezdõcímére mutató pointer.								*
********************************************************************************/

void readStopTimes(Stop* headStop)
{
	FILE* stops_times_fp = fopen("stop_times.txt", "r");
	if (stops_times_fp == NULL)								//ha nem tudja megnyítni a fájlt
	{
		printf("\nError! stop_times.txt not found!\n");
		exit(-1);
		return NULL;
	}
	StopTime* buff = (StopTime*)malloc(sizeof(StopTime));
	Stop* from = NULL;										//melyik megálló elérésébe fogja belerakni
	char* trip_ID = (char*)malloc(11 * sizeof(char));
	int depTime;
	while (readStopTime(stops_times_fp, buff))				//addig olvas ameddig a végéig nem ér a fáljnak
	{
		if (buff->seq == 0)									//ha a járat most indul el a végállomásról
		{
			from = searchStop(buff->stop_ID, headStop);
			strcpy(trip_ID, buff->trip_ID);
			depTime = buff->depTime;
		}
		else if (strcmp(buff->trip_ID, trip_ID) == 0)		//ha elõzõleg és a most beolvasott egy járathoz tartozik
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
		else												//helyes adatok esetén nem fog ide jutni a program
			printf("\nReading ERROR in stop_times.txt!");
	}
	free(buff);
	fclose(stops_times_fp);
	/*free(trip_ID);*/
	printf("Reaches are ready!\n");
	printf("Links are ready!\n");
}

/********************************************************************************
*	Beolvas egy sort a járatokat tároló állományból és eltárolja egy buffer		*
*	elemben. Ha be tudott olvasni akkor vissza ad 1-et, különben 0-át.			*
*	A FILE* fp az állományra mutat, a Trip* buffer-be olvassa be az adatokat.	*
********************************************************************************/

int readTrip(FILE* fp, Trip* buffer)
{
	for (int i = 0; i < 4; i++)
		if (fscanf(fp, "%c", &buffer->route_ID[i]) != 1)	//ha már nem tud beolvasni
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
*	Felépíti a láncolt-listát a beolvasott adatokból. Addig olvassa be az		*
*	adatokat a readTrip() függvénnyel amíg a fálj végéhez nem ér. Végül vissza	*
*	adja a járatokat tároló láncolt-lista kezdõcímét.							*
********************************************************************************/

Trip* readTrips(void)
{
	Trip* headTrip = NULL;
	Trip* buff;
	FILE* trips_fp = fopen("trips.txt", "r");
	if (trips_fp == NULL)								//ha nem tudja megnyítni a fájlt
	{
		printf("\nError! trips.txt not found!\n");
		exit(-1);
		return NULL;
	}
	buff = (Trip*)malloc(sizeof(Trip));
	while (readTrip(trips_fp, buff))					//addig olvas ameddig a végéig nem ér a fáljnak
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
*	Beolvas egy sort a viszonylatokat tároló állományból és eltárolja egy buffer*
*	elemben. Ha be tudott olvasni akkor vissza ad 1-et, különben 0-át.			*
*	A FILE* fp az állományra mutat, a Route* buffer-be olvassa be az adatokat.	*
********************************************************************************/

int readRoute(FILE* fp, Route* buffer)
{
	for (int i = 0; i < 4; i++)
		if (fscanf(fp, "%c", &buffer->ID[i]) != 1)	//ha már nem tud beolvasni
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
*	Felépíti a láncolt-listát a beolvasott adatokból. Addig olvassa be az		*
*	adatokat a readTrip() függvénnyel amíg a fálj végéhez nem ér. Végül vissza	*
*	adja a viszonylatokat tároló láncolt-lista kezdõcímét.						*
********************************************************************************/

Route* readRoutes(void)
{
	Route* headRoute = NULL;
	Route* buff;
	FILE* routes_fp = fopen("routes.txt", "r");
	if (routes_fp == NULL)								//ha nem tudja megnyítni a fájlt
	{
		printf("\nError! routes.txt not found!\n");
		exit(-1);
		return NULL;
	}
	buff = (Route*)malloc(sizeof(Route));
	while (readRoute(routes_fp, buff))					//addig olvas ameddig a végéig nem ér a fáljnak
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
*						Átváltja radiánba a fokot.								*
********************************************************************************/

double DegToRad(double deg)
{
	return deg * (3.14159265358979323846 / 180.0);
}

/********************************************************************************
*	Meghatározza kép megálló távolságát. Stop* s1, Stop* s2 a két megálló.		*
********************************************************************************/

double StopsDist(Stop* s1, Stop* s2)
{
	const int R = 6371000;		//föld sugara
	double lat1 = DegToRad(s1->pos.lat), lon1 = DegToRad(s1->pos.lon), lat2 = DegToRad(s2->pos.lat), lon2 = DegToRad(s2->pos.lon);
	return acos(sin(lat1) * sin(lat2) + cos(lat1) * cos(lat2) * cos(lon2 - lon1)) * R;
}

/********************************************************************************
*	Beállítja két megálló közti távolságot. Ha már 	Stop* stop elérési listájban*
*	létezik Stop* way-be mutató elérés akkor csak a távolságot beállítja int	*
*	dist-re. Küönben létrehoz egy új elérést, amit hozzáad verem módszerrel az	*
*	eddigi elérésekhez és beállítja a távolságot.								*
********************************************************************************/

void SetWalkDist(Stop* stop, Stop* way, int dist)
{
	for (Reach* i = stop->headReach; i != NULL; i = i->nextReach)
		if (i->way == way)											//ha már szerepel az elérései között
		{
			i->dist = dist;
			return;
		}
	Reach* reach = (Reach*)malloc(sizeof(Reach));					//ha még nem szerepe az eléresei között
	reach->way = way;
	reach->dist = dist;
	reach->headLink = NULL;
	reach->nextReach = stop->headReach;
	stop->headReach = reach;
}

/********************************************************************************
*	Megvizsgálj minden megálló párosítást és kiszámolja a párok között lévõ 	*
*	távolságot, ha ez a érték kisebb mint a int maxDist akkor beállítja a kettõ	*
*	távolságát a SetWalkDist() függvénnyel. A Stop* headStop pedig a megállókat	*
*	tároló lácolt-lista kezdõcímére mutató pointer.								*
********************************************************************************/

void CreateWalk(Stop* headStop, int maxDist)
{
	for (Stop* i = headStop; i != NULL; i = i->nextStop)
	{
		for (Stop* j = i->nextStop; j != NULL; j = j->nextStop)
		{
			int dist = StopsDist(i, j);
			if (dist <= maxDist)								//ha két megálló távolsága a maxDist-en belül van
			{
				SetWalkDist(i, j, dist);
				SetWalkDist(j, i, dist);
			}
		}
	}
	printf("Pathways are ready!\n");
}

/********************************************************************************
*	Visszaadja az elsõ olyan Link-et a Reach* reach link-jei közül, ahol az		*
*	indulás idõben késõbb van mint, hogy én oda érnék (int time). És ezek közül	*
*	is a legkorábbit keresem meg, majd visszaadom azt a Linket.					*
********************************************************************************/

Link* GetTheFirstLink(Reach* reach, int time)
{
	int minTime = 36 * 3600;									//felülbecslés
	Link* minLink = NULL;
	for (Link* i = reach->headLink; i != NULL; i = i->nextLink)
	{
		if (i->depTime >= time && minTime >= i->arrTime)		//ha meg tudom javítani a felülbecslést
		{
			minTime = i->arrTime;
			minLink = i;
		}
	}
	return minLink;
}

/********************************************************************************
*	Végigmegy a megállókon és megkeresi azt a megállót amelyett az algoritmus	*
*	még nem tett be a kész halmazba és a megállóba vezetõ út a legrövidebb. Ezt	*
*	a megállót visszaadja az algoritmus. Ha nincs ilyen akkor NULL-t ad vissza.	*
*	A Stop* headStop a megállókat ároló lácolt-lista kezdõcímére mutató pointer.*
********************************************************************************/

Stop* GetMinUnfinshed(Stop* headStop)
{
	int minTime = 100000;																//felülbecslés
	Stop* minStop = NULL;
	for (Stop* i = headStop; i != NULL; i = i->nextStop)
	{
		if (!i->IsDone && i->shortestTime != -1 && i->shortestTime <= minTime)			//ha meg tudom javítani a felülbecslést
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
*	A legrövidebb uatkat keresõ algoritmus, amely élmenti javításokkal dolgozik.*
*	Kezdetben csak a Stop* startStop van az elértek között, az indulási idõvel.	*
*	Utána az akuális megállóból elérhetõ megállókat élmenti javítássokkal		*
*	javítunk az elérési idejeiken. Ezután megkeressük az el nem ért megállók	*
*	közül a legkisebb elérési idejüt a GetMinUnfinshed() függvénnyel.			*
*	A Stop* headStop a megállókat tároló lácolt-lista kezdõcímére mutató pointer*
*	A int startTime az indulási idõ, a double walk_speed a gyaloglás sebbesége.	*
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
	while (!IsDone)																						//addig fut ameddig nem lehet több élmenti javítást végezni
	{
		for (Reach* i = current->headReach; i != NULL; i = i->nextReach)
		{
			Link* firstLink = GetTheFirstLink(i, current->shortestTime);
			if (firstLink != NULL && i->dist == -1)														//1. eset: az adott elérést csak jermûvel lehet megtenni
			{
				if (firstLink->arrTime <= i->way->shortestTime || i->way->shortestTime == -1)
				{
					i->way->shortestTime = firstLink->arrTime;
					i->way->prevStop = current;
					i->way->prevLink = firstLink;
				}
			}
			else if (firstLink != NULL && i->dist != -1)													//2. eset: az adott elérést jármûvel és gyalog is meg lehet tenni
			{
				if (firstLink->arrTime <= (int)(i->dist / walk_speed) + current->shortestTime)			//2/a. eset: ha a jármûvel gyorsabban eljutunk az egyik megállóból a másikba
				{
					if (firstLink->arrTime <= i->way->shortestTime || i->way->shortestTime == -1)
					{
						i->way->shortestTime = firstLink->arrTime;
						i->way->prevStop = current;
						i->way->prevLink = firstLink;
					}
				}
				else if (firstLink->arrTime > (int)(i->dist / walk_speed) + current->shortestTime)		//2/b. eset: gyalog gyorsabb megtenni a távot
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
			else if (firstLink == NULL && i->dist != -1)												//3. eset: az adott elérést csak gyalog lehet megtenni
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
				//seemi se történik
			}
		}
		current = GetMinUnfinshed(headStop);		//következõ aktuális megálló a minimális elérésû lesz
		if (current == NULL)						//ha már nem tudok javítani
			IsDone = 1;
		else
			current->IsDone = 1;
	}
}

/********************************************************************************
*		Kíir a Stop* stop megálló ID-jét, elérési idejét, és nevét.				*
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
*	A végpontól kezdve vissza megy a kezdõ csúcshoz és közben eltárolja, hogy	*
*	az aktuális megállóból melyik a következõ megálló. A Stop* endStop a végpont*
********************************************************************************/

void SetPathNextStops(Stop* endStop)
{
	Stop* before = endStop;
	for (Stop* i = endStop->prevStop; i != NULL; i = i->prevStop)
	{
		i->nextpathStop = before;		//itt állítja be, hogy melyik a következõ megálló az útvonalon
		before = i;
	}
}

/********************************************************************************
*	Megkeresi a járat ID-je alapján a járatot, majd visszaadja a rá mutató		*
*	pointert. Ha nincs ilyen ID-jû járat a listában visszaad egy NULL pointert.	*
*	A char ID[11] jelöli a keresendõ járat ID-jét, a Trip* headTrip pedig a		*
*	járatokat tároló lácolt-lista kezdõcímére mutató pointer.					*
********************************************************************************/

Trip* searchTrip(char ID[11], Trip* headTrip)
{
	for (; headTrip != NULL; headTrip = headTrip->nextTrip)
		if (strcmp(ID, headTrip->ID) == 0)
			return headTrip;
	return NULL;
}

/********************************************************************************
*	Megkeresi a viszonylat ID-je alapján a viszonylatot, majd visszaadja a rá 	*
*	mutató pointert. Ha nincs ilyen ID-jû viszonylat a listában visszaad egy 	*
*	NULL pointert. A char ID[5] jelöli a keresendõ viszonylat ID-jét, a Route*	*
*	headRoute pedig a viszonylatokat tároló lácolt-lista kezdõcímére mutató pntr*
********************************************************************************/

Route* searchRoute(char ID[5], Route* headRoute)
{
	for (; headRoute != NULL; headRoute = headRoute->nextRoute)
		if (strcmp(ID, headRoute->ID) == 0)
			return headRoute;
	return NULL;
}

/********************************************************************************
*	Kiírja a megtalált útvonalat, ahol külön kiírja a járattal való utazást és	*
*	a gyaloglást. A Stop* headStop a megállókat tároló lácolt-lista kezdõcímére	*
*	mutató pointer. A Stop* startStop és a Stop* endStop a két végpont mutatója.*
*	A Route* headRoute pedig a viszonylatokat tároló lácolt-lista kezdõcímére	*
*	mutató pointer. A Trip* headTrip pedig a járatokat tároló lácolt-lista		*
*	kezdõcímére mutató pointer.													*
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
			if (strcmp(i->prevLink->trip_ID, "0000000000") != 0)								//1. eset: egy járaton való utazást írja ki
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
			else if (i->nextpathStop != NULL)													//2a. eset: egy gyaloglást ír ki, amely nem a cél pozicióhoz vezet
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
			else																				//2b. eset: egy gyaloglást ír ki, amely a cél pozicióhoz vezet
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
		else if (strcmp(i->prevLink->trip_ID, "0000000000") == 0 && strcmp("0000000000", i->nextpathStop->prevLink->trip_ID) == 0)		//ha több gyaloglás van egymás után
		{
			dif_dist += walk_speed * (i->shortestTime - time);
		}
		else																															//ha több megállót utazunk egy járattal
			n++;
	}
	printf("\n------------------------------------------------------------------------------------------------------------------------\n");
}

/********************************************************************************
*	Törli a Stop* headStop-ban tárolt Stop*-ok a bejárásra vonatkozó adatokat.	*
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
*	Hozzáadja a Stop* headStop láncolt-listához verem módszerrel a Stop* stop	*
*	megállót, majd visszaadja az új láncolt-listára kezdõcímére mutatóját.		*
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
*					Kírja a lent ltható sémát (BKK Futar).						*
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
*					Kírja a lent ltható sémát (Path not found!).				*
********************************************************************************/

void PrintNotFound(void)
{
	printf("\t\t\t\t\t _____________________________\n");
	printf("\t\t\t\t\t|                             |\n");
	printf("\t\t\t\t\t|       Path not found!       |\n");
	printf("\t\t\t\t\t|_____________________________|\n");
}

/********************************************************************************
*	Bekéri a felhasználótól az indulási idõt, valamint a kezdõ és végpontok GPS	*
*	koordinátáját. A megadott kezdõ- és végpont pointereiben el is tárolja.		*
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
*	Megvizsgálj a kezdõ és végpontok közelében lévõ megállókat és kiszámolja a 	*
*	távolságukat, ha az érték kisebb mint a int maxDist akkor beállítja a két	*
*	megálló távolságát a SetWalkDist() függvénnyel. A Stop* headStop pedig a 	*
*	megállókat tároló lácolt-lista kezdõcímére mutató pointer.					*
********************************************************************************/

void CreateStartEndWalk(Stop* headStop, int maxDist)
{
	int count = 0;
	for (Stop* i = headStop; count < 2; i = i->nextStop, count++)
	{
		for (Stop* j = i->nextStop; j != NULL; j = j->nextStop)
		{
			int dist = StopsDist(i, j);
			if (dist <= maxDist)							//ha a két megálló távolság legfeljebb int maxDist
			{
				SetWalkDist(i, j, dist);
				SetWalkDist(j, i, dist);
			}
		}
	}
}

/********************************************************************************
*		Megérdezi a felhasználótól, hogy szertne-e új keresést indítani.		*
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
*		Felszabadítja a Reach* reach-ben tárolt Link*-ek memóriáját.			*
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
*		Felszabadítja a Stop* stop-ban tárolt Reach*-ek memóriáját.				*
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
*		Felszabadítja a Stop* headStop-ban tárolt Stop*-ok memóriáját.			*
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
*		Felszabadítja a Trip* headTrip-ben tárolt Trip*-ek memóriáját.			*
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
*		Felszabadítja a Route* headRoute-ban tárolt Route*-ok memóriáját.		*
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
	setlocale(LC_ALL, "");										//beállítja hogy magyar karaktereket is ki lehessen írni
	double walk_speed = 1.4;													//gyaloglás sebessége
	int max_walk_dist = 500;													//gyalog megtehetõ maximális távolság két megálló között
	int new_search = 1;
	Stop* headStop = readStops();												//beolvassa a megállókat
	readStopTimes(headStop);													//beolvassa a járatok megállóit
	Trip* headTrip = readTrips();												//beolvassa a járatokat
	Route* headRoute = readRoutes();											//beolvassa a viszonylatokat
	CreateWalk(headStop, max_walk_dist);										//létre hozza a gyalogos kapcsolatokat

	Stop* startStop = NULL, *endStop = NULL;
	Time startTime;

	while (new_search)															//annyiszor keres útvonalat a felhasználó ahányszor csak akar
	{
		startStop = (Stop*)malloc(sizeof(Stop));
		endStop = (Stop*)malloc(sizeof(Stop));

		Input(startStop, endStop, &startTime);									//bekérjük a felhasználótól az indulási idõt, valamint a kezdõ és végpontot

		headStop = AddPoint(headStop, startStop);								//hozzáadjuk a kezdõpontot
		headStop = AddPoint(headStop, endStop);									//hozzáadjuk a végpontot

		CreateStartEndWalk(headStop, max_walk_dist);							//a kezdõ és végpont körüli gyalog elérhetõ csúcsok kapcsolatát beállítjuk

		Dijkstra(headStop, startStop, GetTimeInSec(startTime), walk_speed);		//a keresõ algoritmus futása

		FILE* fp = fopen("outputs.txt", "w");
		DrawLines(fp, headStop);
		fclose(fp);

		if (endStop->IsDone == 0)												//ha nem talált a kezdõ és végpont között útvonalat
			PrintNotFound();
		else																	//ha talált a kezdõ és végpont között útvonalat
			PrintPath(startStop, endStop, headTrip, headRoute, walk_speed);		//kiírja a talált útvonalat

		FreeDijkstra(headStop);													//törli a bejárásra vonatkozó adatokat

		new_search = ISNewSearch();												//megkérdezi a felhasználótól hogy szeretne-e új keresést
	}

	FreeStops(headStop);														//felszabadítja a fõ fésüs adatszerkezetett
	FreeTrips(headTrip);														//felszabadítja a járatokat tároló láncolt-listát
	FreeRoutes(headRoute);														//felszabadítja a viszonylatokat tároló láncolt-listát
	return 0;
}