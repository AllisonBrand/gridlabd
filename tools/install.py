# Syntax: install [OPTIONS ...] ORGANIZATION/REPOSITORY ...
"""GridLAB-D tool installer

Syntax:

    gridlabd install [OPTIONS ...] ORGANIZATION/REPOSITORY ...

Options:

    --branch|-b=BRANCH   select a branch (default is "main")

The installer inspects the repository's install manifest file 'install.json'.
The 'application' tag must be 'gridlabd' and the version must be the same the
current version of 'gridlabd'.  When these conditions are satisfied,
the 'install-command' is executed in a shell, with the 'branch' variable
inserted where specified by the '{branch}' string.

Developer Guide:

Example `install.json` manifest file

    {
      "application" : "gridlabd",
      "version" : 4.3,
      "tool-name" : "my_tool",
      "install-command" : "curl -sL https://raw.githubusercontent.com/dchassin/gridlabd-advisor/{branch}/install.sh | bash"
    }

Example `install.sh` install command

    python3 -m pip install -qq openai
    mkdir -p $HOME/.openai
    curl -sL https://raw.githubusercontent.com/dchassin/gridlabd-advisor/main/advisor.py -o $(gridlabd --version=install)/share/gridlabd/advisor.py || echo "ERROR: install failed" > /dev/stderr
"""

import sys, os
import subprocess
import requests
import json

BRANCH = "main"
INSTALL = []

# process the command line arguments
for arg in sys.argv[1:]:
    spec = arg.split("=")
    tag,value = (spec[0],True) if len(spec) == 1 else (spec[0],'='.join(spec[1:]))
    if tag in ['-h','--help','help']:
      print(__doc__,file=sys.stdout)
      exit(0)
    elif tag in ['-b','--branch'] and type(value) is str:
        BRANCH = value
    elif tag.startswith('-') or len(arg.split("/")) != 2:
        print(f"ERROR [install]: option '{arg}' is invalid")
        exit(1)
    else:
        INSTALL.append(arg)

# check for install items
if not INSTALL:
    print("Syntax: gridlabd install [OPTIONS ...] ORGANIZATION/REPOSITORY ...",file=sys.stderr)
    exit(1)

# run for each install item specified
for item in INSTALL:
    try:

        # get the manifest on the specified branch
        response = requests.get(f"https://raw.githubusercontent.com/{item}/{BRANCH}/install.json")
        if response.status_code != 200:
            raise Exception(f"repository {item} does not contain 'install.json' manifest file")

        # extract the manifest data and check it
        data = json.loads(response.text.encode('utf-8'))
        if data["application"] != "gridlabd":
            raise Exception(f"repository {item} does not contain a gridlabd tool") 
        
        # check the version
        result = subprocess.run(['gridlabd','--version=number'],capture_output=True)
        version = float('.'.join(result.stdout.decode('utf-8').split('.')[0:2]))
        if "version" in data and data["version"] != version:
            raise Exception(f"tool {item} version {data['version']} is not compatible with GridLAB-D version {version}")
        
        # get the tool name
        toolname = data['tool-name'] if "tool-name" in data else item.split('/')[1]

        # run the install command
        if os.system(data["install-command"].format(branch=BRANCH,BRANCH=BRANCH)):
            raise Exception(f"{' '.join(sys.argv)} failed")
        else:
            print(f"Install of {item} done. Run 'gridlabd {toolname} help' for more information.")

    except Exception as err:

        print(f"ERROR [install]: {err}",file=sys.stderr)
        exit(1)

