#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>

#define MAX_INPUT_SIZE 1024
#define MAX_TOKEN_SIZE 64
#define MAX_NUM_TOKENS 64
#define MAX_COMMANDS 10
#define MAX_PROCESSES 64

static char commands[MAX_COMMANDS][MAX_TOKEN_SIZE];
static char server[MAX_TOKEN_SIZE], serverport[MAX_TOKEN_SIZE]; 
static int getfl_processes[MAX_PROCESSES];
static int fgid;

char **globaltokens;
struct cmdline* globalcmd;


enum Tokentype {PIPE, REDIR, BASECOM,FILETYPE};

struct cmdline										// Structure for a command node in the parse tree.
{
	enum Tokentype type;							// One of PIPE, REDIR, BASECOM,FILETYPE
	char** argsv;									// for holding the tokens for this command
	int argsc;										// number of tokens
	int fd;											// When REDIR, fd is 0 for < and 1 for >
	struct cmdline* left;							// Left child for this command 	(LEFT_CHILD [| or < or >] RIGHT_CHILD)
	struct cmdline* right;							// Left child for this command
};

void sigproc()										//signal handler for the main shell
{ 		
	signal(SIGINT, sigproc);
	if(fgid!=0)										// send signal to the process group running in background
	{
		int i;
		killpg(fgid , SIGINT);
		printf("Process id:%d interrupted\n", fgid);		
	}
}

int is_standard(char* token)						//Function to check if the command is one of the linux built in commands
{
	DIR *dir;
	struct dirent *ent;
	int found = 0;
	if ((dir = opendir ("/bin/")) != NULL)			//open /bin/ and check if the given token matches one of the executables
	{					
		while ((ent = readdir (dir)) != NULL) 
		{
			if(!strcmp(token , ent->d_name)){found =1; break;}
		}
		closedir (dir);
	}
	else 
	{
		fprintf(stderr, "Could not open /bin/\n");
		return 0;
	}
	////////couldn't find in /bin, so now check in /usr/bin/
	DIR *dir2;
	struct dirent *ent2;
	if ((dir2 = opendir ("/usr/bin/")) != NULL)
	{					//open /usr/bin/ and check if the given token matches one of the executables
		while ((ent2 = readdir (dir2)) != NULL) 
		{
			if(!strcmp(token , ent2->d_name)){found =2; break;}
		}
		closedir (dir2);
	}
	else 
	{
		fprintf(stderr, "Could not open /usr/bin/\n");
		return 0;
	}
	return found;
}

void freept(struct cmdline* root)			//cmdline* malloced on line 86 is freed here
{
	// printf("going to free %p\n",root );
	if(root==NULL) return;
	if(root->left) freept(root->left);
	if(root->right) freept(root->right);
	free(root);			// no need to free argv 2d array , because they are only pointers to already
						// existing tokens array which was malloced in main. This array will be freed in main.
}

struct cmdline* creatept(char** tokens, int start, int end, int* status)			// Function parses the given command line and creates a parse tree
{
	if(start > end) return NULL;
	int i;
	struct cmdline* res = (struct cmdline*)malloc(sizeof(*res));					//These malloced constructs will be freed in freept
	for(i=start;i<=end;++i)														// Pipes have lowest precedence , hence they appear first in parse tree.
	{
		if(!strcmp(tokens[i],"|"))
		{
			// printf("Found | \n");
			int leftsuccess=0, rightsuccess=0;										// These variables basically tell if the left/right command is correct/valid.
			struct cmdline* leftchild = creatept(tokens, start, i-1, &leftsuccess);		//take the tokens to the left and create parse tree
			struct cmdline* rightchild = creatept(tokens, i+1, end, &rightsuccess);		//take the tokens to the right and create parse tree
			// printf("Left status is : %d\n",leftsuccess);
			// printf("Left status is : %d\n",rightsuccess);
			res->type = PIPE;				// root type is PIPE
			res->argsc = 0;
			res->argsv = NULL;
			res->left = leftchild;			//Link the created left and right children	
			res->right = rightchild;
			if(leftsuccess && rightsuccess)
			{
				*status = 1;
			}
			else			// The given pipe command is valid only if both left and right side are valid
			{
				fprintf(stderr, "Invalid command:");
				int j;
				for(j=start;j<=end;++j)
				{
					fprintf(stderr, "%s ",tokens[j]);
				}
				fprintf(stderr, "\n");
				*status = 0;
				freept(res);				// free the subtrees so far created
				return NULL;
			}		
			return res;
		}
	}

	for(i=start;i<=end;++i)										// next priority in parse tree for REDIR
	{
		if(!strcmp(tokens[i],"<") || !strcmp(tokens[i],">"))							// won't work in cases like ./prog < file1 > file2
		{
			int leftsuccess=0, rightsuccess=0;			
			struct cmdline* leftchild = creatept(tokens, start, i-1, &leftsuccess);
			struct cmdline* rightchild = creatept(tokens, i+2, end,&rightsuccess);
			if(leftchild == NULL)leftsuccess = 1;
			if(rightchild == NULL)rightsuccess = 1;
			res->type = REDIR;
			if(!strcmp(tokens[i],"<"))res->fd = 0;
			else res->fd = 1;
			res->argsc = 1;
			res->argsv = &tokens[i+1];
			res->left = leftchild;
			res->right = rightchild;
			if(leftsuccess && rightsuccess && (end-i)==1)			// exactly one arg after >/<
			{
				*status = 1;
			}
			else
			{
				fprintf(stderr, "Invalid command:");
				int j;
				for(j=start;j<=end;++j)
				{
					fprintf(stderr, "%s ",tokens[j]);
				}
				fprintf(stderr, "\n");
				*status = 0;
				freept(res);				// free the subtrees so far created
				return NULL;
			}
			return res;
		}
	}

	int found = 0;
	found = is_standard(tokens[start]);						//either it is standard command
	if(!strcmp(tokens[start],commands[0]) ||!strcmp(tokens[start],commands[1]) ||!strcmp(tokens[start],commands[2]) ||!strcmp(tokens[start],commands[3]) ||
	!strcmp(tokens[start],commands[4]) ||!strcmp(tokens[start],commands[5]) ||!strcmp(tokens[start],commands[6]))
	{
		found = 1;											// or one of the custom ones
	}
	if(found)														// if found, initialize the node 
	{
		res->type = BASECOM;
		res->argsc = end-start+1;
		res->argsv = &tokens[start];
		res->left = NULL;
		res->right= NULL;
		*status = 1;
		return res;
	}
	else															// else the command is invalid
 	{
 		// printf("here\n");
  		fprintf(stderr, "Invalid command:");
		int j;
		for(j=start;j<=end;++j)
		{
			fprintf(stderr, "%s ",tokens[j]);
		}
		fprintf(stderr, "\n");
		*status = 0;
		// printf("%p\n",res );
		res->left = NULL;
		res->right= NULL;
		freept(res);				// free the subtrees so far created
		// printf("freed\n");
		return NULL;
	}
}

// void display(struct cmdline* root)						
// {

// }

char **tokenize(char *line, int* tokensfound)				//standard function given by ma'am
{
	char **tokens = (char **)malloc(MAX_NUM_TOKENS * sizeof(char *));
	char *token = (char *)malloc(MAX_TOKEN_SIZE * sizeof(char));
	int i, tokenIndex = 0, tokenNo = 0;

	for(i =0; i < strlen(line); i++){

		char readChar = line[i];

		if (readChar == ' ' || readChar == '\n' || readChar == '\t'){
			token[tokenIndex] = '\0';
			if (tokenIndex != 0){
	tokens[tokenNo] = (char*)malloc(MAX_TOKEN_SIZE*sizeof(char));
	strcpy(tokens[tokenNo++], token);
	tokenIndex = 0; 
			}
		} else {
			token[tokenIndex++] = readChar;
		}
	}
 
	free(token);
	tokens[tokenNo] = NULL ;
	*tokensfound = tokenNo;
	return tokens;
}

void cd_implementation(char** tokens ,int tokensfound)				// function to do "cd"
{
	int status;
	if(tokensfound !=2)
	{
		fprintf(stderr,"Wrong use of %s. Usage: cd directoryname\n", tokens[0]);
		return ;
	}
	if((status = chdir(tokens[1])))
	{
		fprintf (stderr, "%s: %s\n",tokens[0], strerror (errno));
		return;
	}
	char presentDir[MAX_TOKEN_SIZE];
	getcwd(presentDir, MAX_TOKEN_SIZE);
	printf("Directory changed to: %s\n", presentDir);
}

void server_implementation(char** tokens, int tokensfound)				//function to do "server"
{
	if(tokensfound !=3)
	{
		fprintf(stderr,"Wrong use of %s. Usage: server server-IP server-port\n", tokens[0]);
		return;
	}
	bzero(server,MAX_TOKEN_SIZE);bzero(serverport,MAX_TOKEN_SIZE);
	strcpy(server, tokens[1]);
	strcpy(serverport, tokens[2]);
	printf("Set server: %s, Server-port: %s\n",server, serverport);
}

void cleanup(struct cmdline* parsetree, char** tokens)			// frees up all alloced memory
{
	freept(parsetree);							// free the parse tree
	// Freeing the allocated memory	
	int i;
	for(i=0;tokens[i]!=NULL;i++)
	{
		free(tokens[i]);
	}
	free(tokens);
}
void cleanup_(char** tokens)					// called in cases when pt has not been created yet, so just free the tokens array
{
	// Freeing the allocated memory	
	int i;
	for(i=0;tokens[i]!=NULL;i++)
	{
		free(tokens[i]);
	}
	free(tokens);
}

void run_command(struct cmdline* cmd)			//main function which classifies on the basis of root command and runs over the parse tree in dfs manner.
{
	if(!cmd) 
		{
			cleanup(globalcmd,globaltokens);
			exit(1);
		}
	int marker;
	enum Tokentype cmdtype = cmd->type;			// to switch on the type of token
	if(cmdtype == PIPE)							
	{
		// printf("hello\n");
		int p[2];
		if(pipe(p)<0)							// creting pipe
		{
			fprintf(stderr, "PIPE: %s\n", strerror (errno));
		}
		int fork1 = fork();						// allot write end of pipe to process one and run the left subtree of curr command in this child
		if(fork1 == 0)
		{
			close(1);
			dup(p[1]);
			close(p[0]);
			close(p[1]);
			run_command(cmd->left);
			cleanup(globalcmd,globaltokens);
			exit(1); 			//if run_command fails to exit
		}
		int fork2 = fork();					// allot read end of pipe to process 2, run the right subtree of curr command in the right subchild.
		if(fork2 == 0)
		{
			close(0);
			dup(p[0]);
			close(p[0]);
			close(p[1]);
			run_command(cmd->right);
			cleanup(globalcmd,globaltokens);
			exit(1);			//if run_command fails to exit
		}
		close(p[0]);					// close the end of pipes in the parent.
		close(p[1]);
		wait();							// reap both forked children.
		wait();
		cleanup(globalcmd,globaltokens);
		exit(0);
	}
	else if(cmdtype ==REDIR)			
	{	
		close(cmd->fd);		// close fd(=0 or 1, based on REDIR type). No need to worry for closing std in/out
							//	in main shell, because this executes only in proxy shell (and it's descendents)
		int fd;
		if((fd = open(cmd->argsv[0], O_WRONLY|O_CREAT | O_TRUNC, S_IRWXO|S_IRWXU|S_IRWXG)) < 0) // open the file, create if not present.
		{
			fprintf(stderr, "Open failed:%s : %s\n",cmd->argsv[0], strerror(errno));
			cleanup(globalcmd,globaltokens);
			exit(1);
		}
		run_command(cmd->left);			//appropriate changes in FD table made, now proceed to the child command(redir has only one child).
		close(fd);
		cleanup(globalcmd,globaltokens);
		exit(0);
	}
	else if(cmdtype == BASECOM)				// One of the base commands
	{
		char** tokens = cmd->argsv;			// start address in the global tokens array
		int tokensfound = cmd->argsc;		// keep a count of your tokens, read char arrays only till tokens+tokensfound
		//Case 0: Command cd 								// simple switch on basis of string comparision
		if(!strcmp(tokens[0], commands[0]))
		{
			cd_implementation(tokens,tokensfound);
		}
		//Case 1: Command server
		else if (!strcmp(tokens[0], commands[1]))
		{
			server_implementation(tokens, tokensfound);
		}
		//Case 2: Command getfl
		else if (!strcmp(tokens[0], commands[2]))
		{
			if(tokensfound != 2)
			{
				fprintf(stderr,"Wrong use of %s. Usage: %s filename\n", tokens[0], tokens[0]);
				cleanup(globalcmd,globaltokens);
				exit(1);
			}

			if(server[0] == 0 || serverport[0] == 0) {
				fprintf(stderr,"Server not configured. User server command to configure server\n");
				cleanup(globalcmd,globaltokens);
				exit(1);
			}
			execl("./get-one-file-sig", "./get-one-file-sig", tokens[1], server, serverport, "display", (char*) NULL);
			cleanup(globalcmd,globaltokens);
			exit(1);	// in case execl fails
		}
		//Case 3: Command getsq
		else if (!strcmp(tokens[0], commands[3]))
		{
			if(tokensfound <2)
			{
				fprintf(stderr,"Wrong use of %s. Usage: %s filename1 filename2 ..\n", tokens[0], tokens[0]);
				cleanup(globalcmd,globaltokens);
				exit(1);
			}
			if(server[0] == 0 || serverport[0] == 0) {
				fprintf(stderr,"Server not configured. User server command to configure server\n");
				cleanup(globalcmd,globaltokens);
				exit(1);
			}
			int i;
			int status;
			for(i=1;i<tokensfound;++i)
			{
				int ret=fork();
				if(ret == 0)
				{
					execl("./get-one-file-sig", "./get-one-file-sig", tokens[i], server, serverport, "nodisplay", (char*) NULL);
					cleanup(globalcmd,globaltokens);
					exit(1);	// in case execl fails
				}
				waitpid(ret, &status, 0);				// reap this child before forking off a new one.(Hence the seq implementation).
			}
			cleanup(globalcmd,globaltokens);
			exit(0);
		}
		//Case 4: Command getpl
		else if (!strcmp(tokens[0], commands[4]))
		{
			if(tokensfound <2)
			{
				fprintf(stderr,"Wrong use of %s. Usage: %s filename1 filename2 ..\n", tokens[0], tokens[0]);
				cleanup(globalcmd,globaltokens);
				exit(1);
			}
			if(server[0] == 0 || serverport[0] == 0) {
				fprintf(stderr,"Server not configured. User server command to configure server\n");
				cleanup(globalcmd,globaltokens);
				exit(1);
			}
			int i;
			int pids[tokensfound-1];
			for(i=1;i<tokensfound;++i)			// fork off new children, make them start work. They all run parallely
			{
				int ret=fork();
				if(ret == 0)
				{
					execl("./get-one-file-sig", "./get-one-file-sig", tokens[i], server, serverport, "nodisplay", (char*) NULL);
					cleanup(globalcmd,globaltokens);
					exit(1);	// in case execl fails
				}
				pids[i-1] = ret;			// store the pids in array 
			} 
			int status;
			for(i=0;i<tokensfound-1;++i)		// all the downloads started in parallel. Reap them as they complete. Note that until
												// all downloads are complete, reaping will not happen. zombies may remain transiently,
												// until all downloads are finished.
			{
				waitpid(pids[i],&status,0);	
			}
			// while(wait() > 0);
			cleanup(globalcmd,globaltokens);
			exit(0);
		}
		//Case 5: Command getbg
		else if (!strcmp(tokens[0], commands[5]))			// the main implementaion of fg/bg is in "main", 
															//where the main thread will either wait for proxy shell
															// or not based on whether the command run is bg or fg.
		{
			if(tokensfound != 2)
			{
				fprintf(stderr,"Wrong use of %s. Usage: %s filename\n", tokens[0], tokens[0]);
				cleanup(globalcmd,globaltokens);
				exit(1);
			}

			if(server[0] == 0 || serverport[0] == 0) {
				fprintf(stderr,"Server not configured. User server command to configure server\n");
				cleanup(globalcmd,globaltokens);
				exit(1);
			}
			int status;
			int ret=fork();
			if(ret == 0)
			{
				execl("./get-one-file-sig", "./get-one-file-sig", tokens[1], server, serverport, "nodisplay", (char*) NULL);
				cleanup(globalcmd,globaltokens);
				exit(1);	// in case execl fails
			}
			waitpid(ret, &status, 0);
			printf("Background process (PID: %d) for downloading %s completed.\n",ret, tokens[1] );

		}
		else if (!strcmp(tokens[0], commands[6]))	// case was actually handled in main itself. This will never be called	
		{
			cleanup(globalcmd,globaltokens);
			exit(0);
		}
//=================================================
		else if((marker = is_standard(tokens[0])))					//has to be one of the standard commands
		{
			int lsargs = tokensfound-1;
			char* args[MAX_NUM_TOKENS];
			bzero(args, MAX_NUM_TOKENS*sizeof(char*));		//empty the argument array 
			int i;
			for(i=0;i<tokensfound;++i)
			{
				args[i] = tokens[i];						// fill it with pointers from the global tokens array, pass it to exec
			}
			args[tokensfound] = NULL;					//array passed to exec has to be NULL terminated
			if(execvp(args[0], args))					// execvp searches for the path too
			{
				fprintf(stderr, "Error: %s\n",strerror(errno));	//if execvp fails, print the error.
			}
			cleanup(globalcmd,globaltokens);
			exit(1);			// in case execv fails
		}
//============================================
		// No such commands exists
		else 
		{
			fprintf(stderr,"%s: Command not found\n",tokens[0] );
			return;
		}
		cleanup(globalcmd,globaltokens);
		exit(0);
	}

}

void exit_implementation(pid_t* bgprocesses)			// when "exit" command is given
{
	int i;
	int status;
	for(i=0;i<MAX_PROCESSES;++i)
	{
		if(bgprocesses[i]!=0)
		{
			killpg(bgprocesses[i] , SIGINT);			// send INT signal to all bg processe.
														// Note: no need to send to fg 
														//process(because at a time only one fg can be present,
														//and when this function is running , the fg process is
														// exit itself.(which is handled from the main shell))
		}
	}
	///SIGINT sent to all bg jobs. now reap them
	for(i=0;i<MAX_PROCESSES;++i)
	{
		if(bgprocesses[i]!=0)
		{
			waitpid(bgprocesses[i],&status,0);			// reap all of them one by one
		}
	}
	// at this stage all bg processes have also been exited and reaped . only the main shell is alive. exit from it in main.
}

void  main(void)
{
	signal(SIGINT, sigproc);
	int i;
	int status;
	char line[MAX_INPUT_SIZE];
	int tokensfound;  
	sprintf(commands[0],"cd");							// strings containing standard commands
	sprintf(commands[1],"server");
	sprintf(commands[2],"getfl");
	sprintf(commands[3],"getsq");
	sprintf(commands[4],"getpl");
	sprintf(commands[5],"getbg");
	sprintf(commands[6],"exit");
	bzero(server, MAX_TOKEN_SIZE);					//setting up global server and serverport vars
	bzero(serverport, MAX_TOKEN_SIZE);
	bzero(getfl_processes, MAX_PROCESSES);
	pid_t bgprocesses[MAX_PROCESSES];					// setting up array for holding bg processes.
	bzero(bgprocesses, MAX_PROCESSES*sizeof(pid_t));
	while (1) {
		printf("Hello>"); 
		bzero(line, MAX_INPUT_SIZE);
		fgets(line,MAX_INPUT_SIZE,stdin);   									///warning: the `gets' function is dangerous and should not be used.
		line[strlen(line)] = '\n'; //terminate with new line
		globaltokens = tokenize(line,&tokensfound);
		for(i=0;i<MAX_PROCESSES;++i)				//periodic reaping
		{
			if(bgprocesses[i] != 0)
			{
				int reapedpid = waitpid(bgprocesses[i] ,&status, WNOHANG);			// wait doesn't block
				if(reapedpid == bgprocesses[i])
				{
					bgprocesses[i] = 0;
				}

			}
		}
		if(globaltokens[0] == NULL) continue;
		// if(!strcmp(tokens[0], "s")){sprintf(line, "server localhost 5000\n");cleanup_(tokens);tokens=tokenize(line, &tokensfound);}
		if(!strcmp(globaltokens[0], "clear"))
		{
			cleanup_(globaltokens);
			printf("%s","\033[2J\033[1;1H" );				//printing out this seq has effects similar to "clear" in standard shell
			continue;
		}
		int validCommand = 0;
		globalcmd = creatept(globaltokens,0,tokensfound-1,&validCommand);	//create parse tree from the given command line
		if(!validCommand) 
		{
			cleanup(globalcmd, globaltokens);
			continue;					// if any error in command , free up resources ,
										// prompt for next command(error in commands will 
										//be printed in creatpt function itself).
		}
		//For utilities implemented in shell itself---- namely cd, server and exit
		if(globalcmd->type == BASECOM)
		{
			if(!strcmp(globaltokens[0], commands[0]))
			{
				cd_implementation(globaltokens, tokensfound);
				cleanup(globalcmd, globaltokens);
				continue;
			}
			else if(!strcmp(globaltokens[0], commands[1]))
			{
				server_implementation(globaltokens, tokensfound);
				cleanup(globalcmd, globaltokens);
				continue;
			}
			else if(!strcmp(globaltokens[0], commands[6]))
			{
				exit_implementation(&bgprocesses[0]);				// sigint sent to all bg processes , they are reaped too
				cleanup(globalcmd, globaltokens);									
				break;
			}
		}
		int status;
		int reaped;
		int proxymain=fork();
		fgid = 0;								// for foreground processes.
		if(globalcmd->type != BASECOM || !is_standard(globalcmd->argsv[0])){setpgid(proxymain,proxymain);}	// seperate the new processes from 
															// main shell only if it is not a base command, or not one of the 
															// standard linux ones.(becuase many linux built in commands like
															// man/vim etc need to have control of I/O of the gnome-termnal.So they need
															// to be either in the same process group, or get the control through 
															//tcsetpgid.However that is proving to be quite complex. So let them be in the 
															// same process group.
		if(proxymain == 0)
		{
			signal(SIGINT, SIG_DFL);	// In the child , map sigint to the default signal handler
			if(globalcmd->type != BASECOM || !is_standard(globalcmd->argsv[0])){setpgid(0,0);}		// same reasons as above
			run_command(globalcmd);			// run the user given command line in the proxy shell(child process of main).
			exit(1);
		}
		else								// in main				
		{
			if(globalcmd->type != BASECOM || (globalcmd->type == BASECOM && strcmp(globaltokens[0], "getbg")) )		//for everything other than getbg, wait
			{
				fgid = proxymain;					// since we are waiting , the process is fg. Set fgid to its process group/pid.
				waitpid(proxymain, &status , 0);			//wait for this proxyshell, and it is running in foreground
				//wait is completed, so fg process(the proxymain is reaped). Effectively no fg exists now.
				fgid = 0;
			}
			else								//for bg process, insert the process in the list of bg processes and continue;
			{
				//else case: the process is not fg. set fgid=0;
				fgid=0;
				for(i=0;i<MAX_PROCESSES;++i)				// search for an empty slot in bgprocesses array and insert pid there.
				{
					if(bgprocesses[i] == 0)
					{
						bgprocesses[i] = proxymain;
						break;
					}
				}
			}
		}
		cleanup(globalcmd, globaltokens);
	}
	exit(0);
}