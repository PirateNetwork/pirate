## Sign a transaction in an offline wallet

## Setup
1. Two wallets are required: One running on a PC with internet access. This we'll 
   call the 'online wallet'. The other on a PC without internet access, called the 
   'offline wallet'. The internet & network disconnected machine will offer a
   physical barrier so that attackers cannot access the spending part of your
   address (private keys).
  
2. Start up the Pirate treasure chest wallet on the
   online machine. Let it finish synchronising with
   the network over the internet.
   zcash parameters: approx 1.6 gb
   block chain: approx 10 gb
   
3. Copy the zcash parameters from the online PC to the
   offline machine. On linux its located in $HOME/.zcash-params
   
4. First run of the offline wallet
   Launch the wallet. The startup splash screen will be displayed, 
   with the status bar showing 'Verifying sprout-verifying.key...' etc.
   
   A popup will appear, stating: 'New installation detected. Press OK
   to download the blockchain bootstrap'. Press 'Cancel'
   
   The main application page will be shown, with the progress window 
   showing how many blockchain block must still be synchronised. 
   Press 'Hide' to get it out of the way
   
   Close the application

5. Navigate to the wallet directory of Treasure
   chest
   On Linux: $HOME/.komodo/PIRATE
   Edit PIRATE.conf
   Add this entry: maxconnections=0
   Save the file
   This will prevent the offline wallet from connecting to the network, even if the machine
   has internet access

## Adreses
1. The philosophy of an offline transaction
   Each adres consists of a public and private part. The public part is what you share
   with people, so that they can make payments to you. The private part is required to
   spend the funds in that adres. This should be kept private at all times. If somebody
   else obtains your private key they can spend the funds. The network will process the 
   transaction if the cryptographic signature is correct, regardless of where the 
   transaction is send from. Your wallet.dat file stores your adresses. An attacker 
   will try to obtain your wallet file in order to steal your private keys, and per 
   extension, your funds. To prevent somebody from launching a remote attack on your 
   computer over the internet or with mallware on your PC, we split up the keys. 
   The wallet on the offline PC contains the full adres, i.e. viewing (public) &
   spending (private). The wallet on the online PC only contains the viewing key.
   If the wallet with the viewing key is stolen its (almost) no deal. You're sharing
   the viewing key with people so that they can make payments to you. The only 
   additional info an attacker with your wallet will obtain is the balance of funds
   in each of your adresses. This is clearly not a desireable thing, but at least they
   will not be able to spend that balance, like they would have if the wallet contained
   the spending (private) part of the adres as well.
   
2. Create an adress in the offline wallet
   Launch the offline wallet. Once verification of the zcash parameters are
   completed, press on 'hide' to hide the blockchain synchronisation window.
   Ignore the status window at the bottom that says the wallet is connecting
   to peers and that you're 12 years behind
   
   Click the 'Receive' button. The adresses on which you can receive funds will 
   be displayed. The 'Mine' column should have a check mark next to it, which
   indicates that the wallet has the spending key (private key) of that adres.
   During the first run of the application it will automatically create a 
   wallet.dat file and generate one adres.
   If you require a new adres to be generated, you can click the 'New' button 
   at the bottom of the window. A popup will appear that states 'New receiving z-address'.   
   Click OK to complete the operation.
   
   You'll notice that the balance next to these adresses are always 0.000. The offline
   PC cannot scan the blockchain to see if any funds were send to its adresses. You'll
   use the online PC for that.
   
3. Exporting an adres to the online wallet
   You need to export the pirate address from your offline wallet to the online wallet. 
   To perform this task, click on the 'Receive' botton to display all your adresses. Righ click on the desired
   adres and select 'export extended viewing key'. A window with about 4 lines of text will
   be displayed, starting with 'zxviews....' Click with the mouse pointer on the text and press
   Ctrl+a to select it all. Then copy and paste it into a text editor. Save the text file and
   transport to the online PC. Since the offline machine is disconnected from all networks for
   security concerns, you'll probably use a USB memory stick.

   Note: The process contains more data than only the pirate address. You share the pirate adress,
   starting with 'zs1...', with people so that they can send pirate coins to you. You use the extended
   viewing key of the pirate adres to add the adres to another wallet, where the balance of the adres
   can be monitored. This will typically only be exported to your online wallet.
   
4. Import extended viewing key in the online wallet
   On the online PC, open the file containing the extended viewing key. 
   Copy the text of the viewing key. Launch the treasure chest application.
   Once it is operational, select File->Import viewing key
   A popup window will appear. Paste the text in the edit box and select OK
   The whole blockchain is scanned by the application to see if there were any funds send to that
   adres in the past. This can take up to an hour on a PC with an Intel i5 processor.
   Once completed, click on the 'Receive' button. The pirate adres should be visible
   in the list. Note that there is no checkmark in the 'Mine' column. It indicates that
   you are only viewing the activity on the adres and that the online wallet doesn't have the 
   spending key (private key) to authorise a spending transaction for that adres. This is exactly
   what we want to accomplish with the split wallet (online & offline) philosophy.

## Transactions   
1. Fund the new address
   You'll first need some funds on the adress before we can illustrate how you authorise payments with 
   the offline wallet. You'll need a friend to send you some pirate coins or you'll need to purchase some
   Pirate coin on an exchange, like TradeOgre, and send the coins to your adres.
   
   If you're using a Treasure chest wallet app, click on the Z-Send tab. Select an adres from the Pay From dropdown
   that has some funds in it. Enter your pirate adres into the 'Pay To' edit box. This is your adress starting with
   'zs1' and not the 'full viewing key' that starts with 'zxviews' Enter the amount, a text memo describing the 
   transaction and the transaction fee.
   Click Send
   Click the Transactions button. You'll see the new transaction at the top of the list. Wait for the network to
   accept the transaction. The hour glass next to the transaction will show the number of network confirmations
   as the transaction is processed. 
   
   If you're withdrawing the coins from an exchange, the exchange will prompt you to enter the adres where you 
   want the coins to be send to and how many of your coins on the exchange you want to transfer to the new
   adres. Enter all the required information and submit the transaction. The exchange will normally show you once 
   the transaction is completed, after it has observed the required number of confirmations from the blockchain 
   network.
   
   On your online wallet, click the Receive button. The amount of the payment will reflect in the balance of the 
   adres. Under 'Transactions' you'll see the transaction entry appear in your log.
        
2. Send coins from your wallet
   To send funds require three actions: 
     1. Construct the transaction on the online wallet. 
     2. Authorise (sign) the transaction on the offline wallet. 
     3. Submit the signed transaction to the pirate network from the online wallet.
   
   Support for signing transactions in an offline wallet is implemented in the command line interface of the 
   wallet, but not yet in the GUI. This part might seem a bit daunting at the outset, but its will soon make 
   sense.
   
2.1 Construct the transaction (online wallet)
   Launch the wallet on the online PC from the command line. The transaction output is printed on the command line.
   If launched from the graphic desktop, i.e. the start button or toherwise, you might not get access to the command
   line output of the application.
   Click on Help->Debug window. Select the 'Console' tab. In the bottom row of the console
   you can enter text commands to the application. All the actions available in the GUI and many more are available in on this 
   console. If you type help<enter> a whole list of possible commands will be displayed. The particular command that we're
   interested in is 'z_sendmany_prepare_offline', 'z_sign_offline' and 'sendrawtransaction'

   You'll need the following pieces of information before you start to construct a transaction:
   - The 'FROM_ADRES' - This is the adres from which the funds will be taken. In this example it is your 
                        viewing adress starting with 'zs1'. You can obtain it by opening the online wallet,
                        click on 'Receive'. Obtain the adres in the list. Right click on the adres and select 
                        'Copy adres'. 
   - The 'RECIPIENT_ADRES' - This is the adres that you are sending the coins to. It can be another adres in your 
                             own wallet that you use to test the transfer of funds, a deposit adres on an exchange, 
                             or an adres of a friend or shop that you want to send coins to
   - AMOUNT - How many coins you want to send. You can specify payments not only up to 2 decimal places, traditionally
              reserved for cents, but up to 6 decimal places. You can therefore send milli (0.001) and micro (0.000001)
              coin payments. In the transaction text is is written as value.fraction, i.e. 12.4 or 0.0024
   - MEMO   - The text memo is a description that you and the recipient can read regarding the transaction. It can 
              for instance contain an invoice number for goods purchased.
   - CONFIRMATIONS - How many network confirmations must occur before the transaction is deemed successful. It is safe
                     to leave this at 1
   - FEE    - How many coins you want to pay the pirate network backoffice in fees for maintaining the pirate network.
              In the transaction text is is written as value.fraction, i.e. 0.000010
   
   It's best to construct the transaction in an external text editor and copy and paste the text into the wallet application.
   Take note of all the funny syntax characters, like [] {} "" and ''
   The text must not contain any extra spaces or newline characters.
   
   The syntax example shows in capital letters where you must substitute the 5 transaction values:   
   z_sendmany_prepare_offline "FROM_ADRES" '[{"address":"RECIPIENT_ADRES","amount":AMOUNT,"memo":"MEMO"}]' CONFIRMATIONS FEE
   
   A populated example, with fake adresses only shown for illustrative purposes:
   z_sendmany_prepare_offline "zs1examplepaymentadresstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789bc" '[{"address":"zs1examplerecipientadresvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789abc","amount":0.001000,"memo":"Test memo"}]' 1 0.000010

   Once you've prepared your transaction, paste the full command into the console of the online wallet.
   If the wallet accepted the command, it will print the following result:
   "generating transaction": "Please wait for the transaction builder to finish. The output is on the shell. Look for the string: z_sign_offline ..."
   
   Go the the console where you've launched the application from. The transaction output will be printed there. It starts with:
   "Paste the full contents into the console of your offline wallet to sign the transaction:"
   The transaction data consists approx. of 2850 text characters. You need to copy the part after the instruction that starts with the 
   command: z_sign_offline....
   
   Paste the full text into a text editor and save it. The file containing the transaction need to be transferred to the offline machine.
   A USB memory stick is probably the easiest way to exchange the file between the online and offline machines.
   
2.2 Authorise the transaction (offline wallet)
   Launch the wallet on the offline PC from the command line. 
   
   Click on Help->Debug window. Select the 'Console' tab. In the bottom row of the console you can enter text commands to
   the application.
   
   Open the file containing the transaction data that was created on the online machine in step 2.1 above.
   Copy and paste the contents into the console. The command starts with: z_sign_offline ....
   
   Wait for the transaction to be signed.  The output is printed in the console window of the wallet. The output contains
   the instruction: "signed transaction: Paste into online wallet without the """
   Copy everything from 'sendrawtransaction' up to the end of the message, excluding the ""
   
   Paste the full text into a text editor and save it. The file containing the signed transaction need to be transferred to 
   the online machine. A USB memory stick is probably the easiest way to exchange the file between the offline and online machines.
   
2.3 Send the signed transaction (online wallet)
   On the online wallet, click on Help->Debug window. Select the 'Console' tab.
   
   Open the file containing the signed transaction data that was created on the offline machine in step 2.2 above.
   Copy and paste the contents into the console. The command starts with: sendrawtransaction ....
   
   The transaction will be submitted to the pirate network for processing.
   
   Close the debug window. In the main application, click on the 'Transactions' button.
   Your transaction should be in the top line of the transaction log. The progress icon 
   will fill up to highlight the progress.

   Congratulations, you've successfully created and send a transaction using an offline wallet
   
