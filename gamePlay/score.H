#ifndef _SCORE_H
#define _SCORE_H

#include<fstream>
#include<string.h>
#include<stdlib.h>
#include<sys/types.h>
#include<pwd.h>
#include<unistd.h>

using namespace std;

class Score
 {typedef int Boolean;
  enum {off, on};

  friend ifstream& operator>>(ifstream&, char* const);
  friend ofstream& operator<<(ofstream&, char* const);

  public:
    Score();
    ~Score();
    int& CurrentScore();
    const char* CurrentName() const;
    int TopTenScore(const int i) const;
    const char* TopTenName(const int i) const;
    int NumScores() const;
    Score& UpdateTopTen();
    Score& Reset();

  private:
    void UpdateScoreArray();

    Boolean scoreUpdated;
    int currentScore,
        score[10],
        numScores;
    char currentName[9],
         name[10][9],
       * fileName;
 };

// Extracts one name token into s (max 8 chars + terminator). Accepts BOTH
// encodings: the native NUL-delimited format this class writes (operator<<)
// and plain whitespace-delimited text. The delimiter (NUL or whitespace) is
// consumed; extraction of zero characters leaves the stream failed so callers
// counting successful extractions terminate on the first incomplete entry.
inline ifstream& operator>>(ifstream& f, char* const s)
 {int i=0;
  char ch;
  while (f.get(ch)&&(ch=='\n'||ch=='\t'||ch==' '));
  if (f&&ch!='\0')
   {s[i++]=ch;
    while (i<8&&f.get(ch)&&ch!='\0'&&ch!='\n'&&ch!='\t'&&ch!=' ')
      s[i++]=ch;
    while (ch!='\0'&&ch!='\n'&&ch!='\t'&&ch!=' '&&f.get(ch));
   }
  s[i]='\0';
  return f;
 }

inline ofstream& operator<<(ofstream& f, char* const s)
 {int i=0;
  while(s[i])f.put(s[i++]);
  f.put(s[i]);
  return f;
 }

inline Score::Score(): scoreUpdated(off),
                       currentScore(0),
                       numScores(0)
 {passwd* userData=getpwuid(geteuid());
  const char* login=userData?userData->pw_name:getenv("USER");
  strncpy(currentName,login?login:"unknown",8)[8]='\0';
  char* cwd=getcwd(new char[3000],3000);
  int fnLen2=strlen(userData?userData->pw_dir:"")
             +strlen("/XAsteroids/hiScore.data"),
      fnLen3=strlen(cwd)
             +strlen("/XAsteroids/hiScore.data"),
      fnLen4=strlen(cwd)
             +strlen("/hiScore.data");
  fileName=new char[(fnLen2>(fnLen3=fnLen3>fnLen4
                                                    ? fnLen3
                                                    : fnLen4)
                                     ? fnLen2
                                     : fnLen3)+1];
  *fileName='\0';
  if (userData)
    strcat(strcpy(fileName,userData->pw_dir),"/XAsteroids/hiScore.data");
  if (!*fileName||access(fileName,F_OK))
    strcat(strcpy(fileName,cwd),"/XAsteroids/hiScore.data");
  if (access(fileName,F_OK))
    strcat(strcpy(fileName,cwd),"/hiScore.data");
  delete [] cwd;
  ifstream file(fileName,ios::in);
  if (!file)
    return;
  while (numScores<10&&(file>>name[numScores]>>score[numScores]))
    ++numScores;
 }

inline void Score::UpdateScoreArray()
 {if (numScores<(int)(sizeof(score)/sizeof(int)))
   {name[numScores][0]='\0';
    score[numScores++]=-1;
   }
 for (int i=0;i<numScores;++i)
    if (currentScore>score[i])
     {for (int j=numScores<(int)(sizeof(score)/sizeof(int))
                 ? numScores
                 : sizeof(score)/sizeof(int)-1;j>i;--j)
       {strcpy(name[j],name[j-1]);
        score[j]=score[j-1];
       }
      strcpy(name[i],currentName);
      score[i]=currentScore;
      break;
     }
 }

inline Score& Score::UpdateTopTen()
 {if (!scoreUpdated)
   {ifstream ifile(fileName,ios::in);
    if (ifile)
     {numScores=0;
      while (numScores<10&&(ifile>>name[numScores]>>score[numScores]))
        ++numScores;
     }
    UpdateScoreArray();
    ofstream ofile(fileName,ios::out);
    for (int i=0;i<numScores;++i)
      ofile<<name[i]<<score[i];
    scoreUpdated=on;
   }
  return *this;
 }

inline Score::~Score()
 {UpdateTopTen();
  delete [] fileName;
 }

inline int& Score::CurrentScore()
 {return currentScore;
 }

inline const char* Score::CurrentName() const
 {return currentName;
 }

inline int Score::TopTenScore(const int i) const
 {return score[i];
 }

inline const char* Score::TopTenName(const int i) const
 {return name[i];
 }

inline int Score::NumScores() const
 {return numScores;
 }

inline Score& Score::Reset()
 {currentScore=0;
  scoreUpdated=off;
  return *this;
 }

#endif
