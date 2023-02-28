#!/bin/bash
#
# Git Config Change Post Receive Hook Script
#
#    - For Identifying Key Beamline Configuration Changes at SNS/HFIR
#
# E.g.:
#    - Any Changes to the "beamline.xml" file
#    - ADARA Deployment Release Notes
#    - ADARA Live Configuration Parameter Changes
#

echo "Entry post-receive-config-change-hook.sh"

KEY_CONFIG_TARGETS="beamline\.xml \
	sms/deployment\.history\.txt stc\.deployment\.history\.txt"

NO_CONFIG_CHANGE_NOTIFY_KEY="NO_NOTIFY"

KEYS_FOUND=0

EMAIL_SUBJECT="SNS/HFIR Configuration Change Notification"

EMAIL_BODY="\n=== ${EMAIL_SUBJECT} ===\n"

# Set GIT_DIR either from working directory, or from environment variable.
projectdesc="UNNAMED PROJECT"
GIT_DIR=$(git rev-parse --git-dir 2>/dev/null)
if [ -n "$GIT_DIR" ]; then
	projectdesc=$(sed -ne '1p' "$GIT_DIR/description" 2>/dev/null)
fi
EMAIL_BODY="${EMAIL_BODY}\n${projectdesc} Git Repository\n"

PROCESS_REVISIONS()
{
	oldrev="$1"
	newrev="$2"
	refname="$3"

	#echo -e "\nProcessing Revisions:"
	#echo "oldrev = [$oldrev]"
	#echo "newrev = [$newrev]"
	#echo "refname = [$refname]"
	#echo

	short_refname=`basename "${refname}"`

	#echo "short_refname=[${short_refname}]"

	EMAIL_BODY="${EMAIL_BODY}\nBranch/Tag '${short_refname}' Updated.\n"

	revlist=`git rev-list ${oldrev}..${newrev}`

	#echo "Revision List: [${revlist}]"

	for rev in ${revlist} ; do

		echo "Revision: [${rev}]"

		revstat=`git diff-tree --stat ${rev}`

		#echo "Revision Stat: [${revstat}]"

		for key in ${KEY_CONFIG_TARGETS} ; do

			#echo "Key: [${key}]"

			if [[ ${revstat} =~ ${key} ]]; then

				echo "Found '${key}' File Reference."

				gitlog=`git show --no-color -s --pretty=medium ${rev}`

				if [[ ${gitlog} =~ ${NO_CONFIG_CHANGE_NOTIFY_KEY} ]]; then
					echo -n "Found \"No Notify\" Directive in Log"
					echo " - Ignoring Revision..."
					continue
				fi

				EMAIL_BODY="${EMAIL_BODY}\nConfiguration Change"
				EMAIL_BODY="${EMAIL_BODY} to '${key}':\n"

				EMAIL_BODY="${EMAIL_BODY}\n${gitlog}\n"

				gitdiff=`git diff-tree --patch ${rev} -- ${key}`

				EMAIL_BODY="${EMAIL_BODY}\n${gitdiff}\n"

				KEYS_FOUND=$(( KEYS_FOUND + 1 ))

			fi

		done

	done
}

send_mail()
{
	if [ -n "$envelopesender" ]; then
		/usr/sbin/sendmail -t -f "$envelopesender"
	else
		/usr/sbin/sendmail -t
	fi
}

# Allow dual mode: run from the command line just like the update hook,
# or if no arguments are given then run as a hook script

if [ -n "$1" -a -n "$2" -a -n "$3" ]; then

	#echo "Command Line Input:"

	oldrev="$2"
	newrev="$3"
	refname="$1"

	#echo "oldrev = [$oldrev]"
	#echo "newrev = [$newrev]"
	#echo "refname = [$refname]"

	PROCESS_REVISIONS "${oldrev}" "${newrev}" "${refname}"

else

	while read oldrev newrev refname ; do

		#echo "Read Input Line:"
		#echo "oldrev=[$oldrev]"
		#echo "newrev=[$newrev]"
		#echo "refname=[$refname]"

		PROCESS_REVISIONS "${oldrev}" "${newrev}" "${refname}"

	done

fi

generate_email()
{
	# --- Email (all stdout will be the email)

	echo "To: $recipients"
	echo "Subject: ${EMAIL_SUBJECT}"
	echo "Auto-Submitted: auto-generated"
	echo -e "${EMAIL_BODY}"
}

if [[ ${KEYS_FOUND} -gt 0 ]]; then

	echo "${KEYS_FOUND} Key Targets Found."

	#echo -e "Email Subject: [${EMAIL_SUBJECT}]\n"

	#echo -e "Email Body: [${EMAIL_BODY}]\n"

	recipients=$(git config hooks.configchangemailinglist)
	envelopesender=$(git config hooks.envelopesender)

	echo -e "Recipients: [${recipients}]"

	generate_email | send_mail

else

	echo "No Key Targets Found."

fi

echo "Exit post-receive-config-change-hook.sh"

