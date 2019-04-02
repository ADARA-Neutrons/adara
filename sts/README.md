### Streaming Translation Service Production Repo
These git tips are not meant to be comprehensive. It is dangerous to
type in these commands without context. You *cannot* learn git in these
5 easy steps :)

#### 5 Second Git:
1. git pull (fetches and merges in upstream)
2. git add  (Adds the new files you added for staging)
3. git commit -m "your message" -a (commits to your local area)
4. git push (pushes up to origin/master)

#### 5 Second Branches: (oversimplified, lots of things to go wrong here)
1. git branch mycoolbranch (creates a new branch locally)
2. git checkout mycoolbranch (switches to that branch for work)
3. Do your normal work, commits, adds..
4. git checkout master (swiches back to the master branch)
5. git merge mycoolbranch (merges in the changes)

#### Remotes [OBSOLETE!!]
1. git remote add sts-devel git+ssh://trac.sns.gov/var/repos/sts-devel
   (creates a new remote called "sts-devel" which points to that repo)
2. git pull sts-devel master 
   (feches and merges in changes from the master branch of sts-devel)
3. git push origin master
   (pushes changes back to the original repo, master branch)
