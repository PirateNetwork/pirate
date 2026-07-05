## Sign a transaction in an offline wallet
This README matches the behaviour of Treasure Chest v5.6

## Introduction
The problem with traditional wallets and how piratechain.com addresses these
shortfalls:
1. How traditional wallets work
   A file called 'wallet.dat' stores your addresses. Each address consists of a public
   and private part. The public part is what you share with people, so they can make
   payments to you. The private part (key) is required to unlock and spend the
   funds associated with that address. 
   
2. Risk associated with traditional wallets
   If somebody obtains your private key they can spend your funds. The network will
   process the transaction if the cryptographic signature is correct. The network
   doesn't validate who sent the transaction or from where.
   An attacker will aim to obtain your wallet file in order to steal your private 
   keys, and per extension, your funds. Malware hidden in applications is an easy
   way for hackers to scout your hard drive for wallet files, which they send to
   themselves over your internet link.
   
   Apart from the risk of loosing your funds to an attacker it's also much more likely
   you'll loose your wallet file due to neglect. There are countless horror stories
   of people who have lost billions (yes, with a 'B') worth of Bitcoin due to
   hard drive crashes, hard drives that were formatted to make room for a new
   O/S or game installation or simply old machines that were thrown away.   
   
   If you were diligent and made a backup of the wallet file, you have to remember
   which addresses it contains. Each time you create a new addresses it is not 
   automatically added to the backup copy. 
   Upon restoring of an old backup you might discover that it doesn't contain all 
   the new addresses created since the backup. Access to those funds are lost forever. 
   
3. BIP39: Mnemonic phrase improvement
   The problem of loosing addresses and their private keys were mitigated with the
   implementation of BIP39 mnemonic seed phrases. A single master key is created.
   All the addresses in that wallet are derived from the master key. To the user
   the master key is presented as a random 24 word English phrase, called a mnemonic.
   
   When the wallet is created this phrase must be written down and stored in a secure
   manner. The storage medium must protect against fire and moisture damage.
   
   Do not use your phone to take a picture of the phrase. Synchronisation software 
   could upload your images to the 'cloud' where it can be intercepted in transit
   or on the remote storage. Do not print it out either. In rare cases malware in
   printer firmware has been found to recognise these seed phrases and send it of
   to the hackers.
   
   Paranoia regarding protection of your seed phrase is not unwarrented.
   
   When it's time to restore your addresses in a new wallet all you have to do is
   to type in the mnemonic. The application will convert it to the master key.
   No software backup of the wallet file is required. By initialising the wallet
   with a valid seed phrase all the addresses derived from it can be recreated.
   
   If you have funds in a traditional wallet thats not based on BIP39, you can
   setup a second wallet that uses the BIP39 technology. You can pay the funds
   from the old wallet addresses to the new wallet, thereby transferring the 
   funds to the new wallet.
   
4. Encrypted wallets
   As seen above, BIP39 guards against loosing access to your individual addresses.
   It however does not provide additional protection of loosing the actual wallet
   file to an attacker. 
   On Treasure Chest the wallet file can be encrypted too. If an attacker obtains
   the wallet file, they will not be able to open it to get the master seed or 
   any of the address data.
   This file is password protected. Your security is based on choosing a long secure 
   password which can withstand a brute force attack. 
      
5. Offline cold storage
   Two computers are required for this setup. One is connected to the internet
   while the other has no network connectivity. 

   The machine without network access is called the offline machine. On it you
   setup an encrypted wallet with mnemonic seed phrase. The machine doesn't have
   a copy of the blockchain.

   The machine with internet access is called the online machine. You set up an
   encrypted wallet with a different mnemonic seed phrase on this machine. You
   won't use any of the addresses derived from its  mnemonic. This machine is
   synchronised with the blockchain.
   
   This process adds two levels of complexity:
   *  How do you view the transactions & balance of an offline address?
   *  How do you spend the funds in an offline address?
   
   Pirate's Treasure Chest wallet overcomes these obstacles as follow:

   Viewing the transaction history
   -------------------------------
   The Pirate blockchain is encrypted. None of the transaction data is visible on
   the blockchain. You can't import the public address into the online wallet and
   view the activity that occurred on that address.

   To overcome the limitation imposed by the encrypted blockchain, Pirate splits 
   the public part of the address in two:
   Spending address: (Starts with 'zs1') This is what you share with somebody to pay you.
                   The sender can not obtain further information regarding the balance
                   of that address or the transaction history of the address.
   Extended viewing key: (Starts with 'zxviews') The extended viewing key contains the
                   spending address and additional information enabling an online wallet
                   to reconstruct the balance of the address and the transaction history.
                   You normally reserve this functionality for private use or to share
                   the address data with a partner or accountant.
   
   The viewing keys are generated by the offline wallet. The viewing keys of selected
   addresses are imported in the online wallet. By analysing the blockchain the online
   wallet can reconstruct the transaction history and balance of the address.
   
   If the online wallet is somehow stolen it's (almost) no deal - information
   is leaked regarding the balance & transaction history of your addresses. This is
   clearly not a desireable thing, but at least the attacker will not be able to spend
   the funds, like they would have if the wallet contained the spending (private)
   key of the address as well.
   
   Spend the funds of an extended viewing key
   ------------------------------------------
   In Treasure Chest the online wallet can construct a transaction with only the viewing
   key of the address. All the required blockchain inputs are contained in this transaction 
   request. With a memory stick the data is transferred to the offline machine. There the
   private key of the address authorises (signs) the transaction. The authorised
   transaction is transferred back to the online wallet and submitted to the blockchain
   for processing.
   
6. Communicatin
   The Treasure Chest wallet supports encryption between the wallet application and the 
   network miner nodes. Your ISP and other information gatherers on the internet won't
   be able to figure out that you're transacting in Pirate coin with your neighbour or
   somebody on the otherside of the globe. In an age of unpresidented survailence Pirate
   offers superior annonymity when a transaction is executed and leaves no visible 
   trace for analytics on the blockchain as a historic record.
   
7. Summary
   Pirate's Treasure Chest wallet gives you access to these features today:
   * BIP39 master seed 
   * Split addresses between two wallets. The offline contains the private keys
     and the online the viewing and spending keys.
   * Encrypted storage of the wallet on disk and encrypted communication
   * All running on an encrypted blockchain   

## Software setup
1. You require two PCs, preferrably Intel i5 or faster. The online machine requires
   30GB of hard drive space. The internet link should run at 10mbps or faster. 
   The machines can run Linux, Mac OS X or MS Windows. For this tutorial Linux is used.
   
2. You can obtain the software from the official Pirate website, piratechain.com:
   https://piratechain.com/wallets/treasure-chest or a source copy from GitHUB at
   https://github.com/PirateNetwork/pirate 
   
   Furthermore, if you do not have a copy of the blockchain, you can download a 
   bootstrap file from the website too. At the time of writing (January 2023)
   the file approached 22GB, with the full blockchain at 28GB.
   The wallet software itself has an option to download this file, but if the
   transfer gets interrupted you'll have to start from the beginning again. 
   With a utility like 'wget' or 'curl'you can use the resume option to continue
   downloading. On a 10mbps link it takes about 4 hours to download.
   Filename: ARRR-bootstrap.tar.gz
        
4 Online machine
4.1 Installation
   Now that you've got the Treasure chest software and blockchain bootstrap, 
   lets continue setting up the online machine
   
   If you've downloaded the Debian Linux install archive, install it as root user:
   # dpkg -i pirate-qt-ubuntu1804-v5.7.5.deb
   The application binary is installed in /usr/local/bin/pirate-qt
   
  Treasure chest will create two sub directories in your $HOME directory where all 
  the data is stored:
      ~/.zcash-params  -- Contains ZKsnark network parameters
      ~/.komodo/PIRATE -- Contains your wallet and the blockchain
      
  Note: Significant improvements were made in version 5.6, that is not compatible
        with 5.5 or older. We recommend that you always run the latest release.

4.2 First run
    From a terminal console, launch the application: pirate-qt
    The wallet will immediately start to download the zk-SNARKs network parameters.
    The download is 750mb and will be located in $HOME/.zcash-params
    On a 10mbps link it takes about 10 minutes to download.
    
    With no files present in the ~/.komodo/PIRATE directory the application will ask
    a couple of setup questions:
    1. A popup appears with the title: New installation is detected.
       The dialog asks if you want to run in online mode or offline mode.
       Press 'Yes' to select online mode.
    2. A popup appears with the title: New installation is detected.
       The dialog asks if you want to download the blockchain bootstrap file
       or synchronise directy from the network peers nodes.
       Select 'Cancel' to synchronise the blockchain from peer nodes. 
       We'll use the ARRR-bootstrap.tar.gz in the following steps.
       
    3. The mnemonic setup dialog appears. It asks how to initialise the mnemonic
       master seed of the wallet. Select 'Create new wallet'. 
       For secuirty reasons we do not use the addresses from the online wallet.
       For the online wallet you can discard the mnemonic that is generated and
       shown to you. If these addresses are compromised there shouldn't be any
       funds in them and no material loss to you.
       
    4. The main application window appears, indicating that the configuration
       was completed. The network synchronisation dialog shows that we're
       catching up with the latest data in the blockchain. Press the hide
       button to minimise the dialog.
       
       Navigate to Settings, Encrypt wallet
       Enter a long secure password to encrypt your wallet. You'll need to
       enter this password each time you launch Treasure Chest. Make sure it's
       something thats still practical for you to type.
       
       The display is locked. Press the unlock button and enter your password.

       Navigate to Settings->Options...
         Select the Wallet tab.
         Under Wallet Functions, make sure these radio button are checked:
           * Enable offline signing
             * Spend (Online mode)
         Select the Network tab
         Make sure these radio button are checked:
           * Only connect to encrypted peers.
       Press OK to apply the configuration. If any of the options were changed
       the application will shutdown, otherwise select File, Exit
             
       Always shutdown the application properly to prevent corruption of the 
       blockchain files. The blockchain files are created but does not yet
       contain any data.

4.3 Blockchain bootstrap
    On a terminal console, navigate to $HOME/.komodo/PIRATE.
         $ cd ~
         $ cd .komodo/PIRATE
    Remove the newly created blockchain files
         $ rm -rf blocks
         $ rm -rf chainstate
    Extract the bootstrap archive, assuming that it's located in your ~/Downloads 
    directory:
         $ tar -xvzf ~/Download/ARRR-bootstrap.tar.gz
    The 14GB archive takes about 3 minutes to extract. The extracted files and
    archive together takes up about 30 GB of harddrive space. Do not let your PC
    run out of HDD space while Treasure Chest is running, or the blockchain files
    will be corrupted.

4.4 Second run
    From a terminal console, run the application: pirate-qt
    The main splash screen will show the verifications done on the blockchain data 
    as it loads it: 
      Loading guts
      Loading block index Db
      Checking all blk files are present 
     
    Since the wallet contains a new seedphrase, the application will still scan 
    the blockchain to see if there are any transactions associated with the addresses 
    in the wallet:
      Rescanning: Currently on block xxxxx
      This takes about 25 minutes to complete on an Intel i5 machine.
      
    On the main application window the synchronisation dialog will show that you're 
    a couple of days behind, depending on how old the bootstrap file was. The 
    synchronisation should complete fairly quickly by bringing you up to date with
    the network.
      
5 Offline machine
5.1 Installation
   The Treasure Chest wallet requires the ZKsnark network parameters.
   
   From the online machine, transfer the Treasure Chest installation archive and
   ~/.zcash-params to the offline machine using a memory stick.

   Copy the zcash parameters to ~/.zcash-params on the offline machine
   
   If you've downloaded the Debian Linux install archive, install it as root user:
   # dpkg -i pirate-qt-ubuntu1804-v5.7.5.deb
   The application binary is installed in /usr/local/bin/pirate-qt
  
5.2 First run
    From a terminal console, run the application: pirate-qt
    The application will load the provided ZKsnark network parameters.
    
    With no config files in the ~/.komodo/PIRATE directory the application will ask
    a couple of setup questions:
    1. A popup appears with the title: New installation is detected.
       The dialog asks if you want to run in online mode or offline mode.
       Press 'No' to select offline mode.    
       
    2. The wallet setup dialog appears. 
       If you have a BIP39 mnemonic seed phrase you'd like to use, select
       the 'Restore wallet from seed' option.
       If you'd like to create a new mnemonic seed phrase, select 'Create
       a new wallet'.
   
       As discussed in the introduction, if you create a new mnemonic it
       is very important that you store the words in a way that is physically
       secure and not use digital equipment that can expose your data to a
       remote attack.
       
       Note: All your addresses, with their private keys, are derived
             from this master seed phrase. Do not use this mnemonic in your
             online wallet, since all the addresses derived from it could
             be available to an attacker.
             
    3. The main application window appears, indicating that the configuration
       was completed. Notice that the network synchronisation dialog is 
       not shown, since we're running in offline mode.

       Navigate to Settings, Encrypt wallet
       Enter a long secure password to encrypt your wallet. You'll need to
       enter this password each time you launch Treasure Chest. Make sure it's
       something that's still practical for you to type.
       
       The display is locked. Press the unlock button and enter your password.       
       
       Navigate to Settings->Options...
         Select the Wallet tab.
         Under Wallet Functions, make sure these radio button are checked:
           * Enable offline signing
             * Sign (Offline mode)
       If the config options were changed the application needs to restart
       to apply the new configuration.
       
6. Backups
   To backup your current setup, shut down Treasure Chest. Then proceed to backup
   the ~/.zcash-params and ~/.komodo directories.
   If you ever extract these directories again and launch Treasure Chest you'll be
   precisely at the state as when the backup was made. Then you'll just have to
   wait for Treasure Chest to resynchronise with the blockchain and you're ready
   to make transactions again.
   
7. Changes from the previous Treasure Chest version
   Previously you had to manually edit the PIRATE.conf file in the
   project home directory to enable the 'offline mode' of the 
   application. This was accomplished by setting the config entry:
   maxconnections=0.
   With Treasure Chest 5.6 the configuration is done in the GUI.
   The config option is accessible from the Settings->Options menu,
   under the Wallet tab.
   
## Address exchange    
1. Create an address in the offline wallet
   Click the 'Receive' button. The public addresses, starting with z x 1 are
   display. People use this address to send funds to you. The 'Mine' column
   should have a check mark next to each address, which indicates that the
   wallet has the private key of that address.
   
   During the first run of the application an address is generated automatically.
   If you require another address you can click the 'New' button at the bottom
   of the window. A popup will appear that prompt for a label for the address.
   The labels can help you to organise your addresses, for instance, to whom
   you've given this address in order to receive a payment. 
   Click OK to complete the operation. The labels will not be restored if you
   restore the wallet from a BIP39 seed phrase.

   The offline PC cannot scan the  blockchain to see the transactions that were
   send to it's addresses. You'll use the online PC to view the balance of your
   addresses.

2. Export an address to the online wallet
   You need to export the extended viewing key of the address from your offline wallet
   to the online wallet. To perform this task, click on the 'Receive' button. Righ
   click on the desired address and select 'export extended viewing key'. A window
   with about 4 lines of text will be displayed, starting with 'zxviews....'
   Click with the mouse pointer on the text and press Ctrl+a to select all the text.
   Then copy and paste it into a text editor. Save the text file and transport it
   to the online PC. Since the offline machine is disconnected from all networks,
   for security concerns, you'll probably use a USB memory stick to transport the
   file to the online machine.

   Note: The viewing key contains more data than only the public address. You share
   the public address, starting with 'zs1...', with people so that they can send
   pirate coins to you. You use the extended viewing key to monitor the balance 
   and transaction history of the address and to request offline transaction 
   signing for that address when you want to spend it's funds. This will only be 
   used in your online wallet.

3. Import extended viewing key in the online wallet
   On the online PC, open the file containing the extended viewing key.
   Copy the text of the viewing key. Launch the Treasure Chest application.
   Once it is operational, select File->Import viewing key
   A popup window will appear. Paste the text in the edit box and select OK
   The whole blockchain is scanned by the application to see if there were any funds
   send to that address in the past. This takes approx. 25 minuts on a PC with an Intel
   i5 processor. Once completed, click on the 'Receive' button. The new address should be 
   visible in the list. Note that there is no checkmark in the 'Mine' column. It indicates
   that you are only viewing the activity on the address and that the online wallet doesn't
   have the spending key (private key) to authorise a transaction for that address. This is
   exactly what we want to accomplish with the split wallet (online & offline) philosophy.

   Even though all the addresses are derived from the BIP39 seed phrase in the 
   offline wallet, you'll have to import the viewing keys one by one into the
   online wallet. There's unfortunately not a corresponding 'viewing only'
   mnemonic that can be used in the online wallet to speed up the process.

## Transactions   
                                       ATTENTION
   For this demonstration, please use very small amounts, like 2 or 3 milliArrr
   at a time. In the event that something goes wrong it's not a big deal in terms
   of lost funds. As you get more comfortable with the process you can increase
   the amount. Make especially sure that you can spend the funds that you've 
   received again. It's no use having a big balance on an address with the funds
   locked in it.

1. Fund the new address
   You'll first need some funds on the address before we can illustrate how you
   authorise payments with the offline wallet. You'll need a friend to send you
   some pirate coins or you'll need to purchase some Pirate coin on an exchange,
   like TradeOgre, and send the coins to your address.

   If you're using a Treasure chest wallet app, click on the Z-Send tab. Select
   an address from the Pay From dropdown that has some funds in it. Enter your 
   public address into the 'Pay To' edit box. This is your address starting with
   'zs1' and not the 'full viewing key' that starts with 'zxviews' Enter the 
   amount, a text memo describing the transaction and the transaction fee for 
   the miners. Note: Leave the miner fee as is. By decreasing it you run the risk
   that the miners will not process your transaction.
   Click Send and complete the transaction.
   Click the Transactions button. You'll see the new transaction at the top of
   the list. Wait for the network to accept the transaction. The clock icon next
   to the transaction will show the number of network confirmations as the 
   transaction is processed.

   If you're withdrawing the coins from an exchange, the exchange will prompt you
   to enter an address. Enter one of your public address into the field. Decide
   how many of your coins on the exchange you want to transfer to the new address.
   Enter all the required information and submit the transaction. The exchange will
   provide you with the transaction id (txid) once the transaction is submitted
   to the network for processing.

   On your online wallet, click the 'Transactions' button. Once the network has
   processed the transaction and recorded it in the blockchain you'll see the
   transaction entry appear in your log. You can spend the funds after 6
   network confirmations.

2. Send coins from your wallet using the GUI
   To send funds require three actions:
     1. Construct the transaction on the online wallet.
     2. Authorise (sign) the transaction on the offline wallet.
     3. Submit the signed transaction to the network from the online wallet.

2.1 Construct the transaction (online wallet)
   Launch the wallet on the online PC. 
   The following configuration must be active for the wallet to be in online
   mode:  Settings->Options, Wallet tab: 
            Enable Offline Signing
              Spend (Online mode)

   Click the Z-Send button to open the send page
   Fill in the fields:
   Pay From: Select an address that is marked 'Off-line'. Only addresses with
             a balance are shown.
   Pay To:   Fill in the recipient address
             If you want to send the funds to yourself, copy one of your own
             addresses from the 'Receive' page. The 'Pay From' and 'Pay To'
             addresses must differ.
   Amount:   The amount of coins to send.
   Memo:     Optionally a memo describing the transaction, like an invoice nr
   Transaction Fee: The amount of coin you want to pay the miners to process
                    your transaction. You can leave it on the default 
   Press the 'Prepare offline transaction' button. The confirmation window 
   appears. Press 'Yes' to continue. The 'offline transaction signing'
   window will appear. In the top text area the prepared transaction data will
   be displayed under the heading: 'Unsigned transaction'.
   Copy this text and store it in a text file and transport it to the offline
   machine. A USB memory stick is probably the easiest way to exchange the file
   between the two machines.

2.2 Authorise the transaction (offline wallet)
   Launch the wallet on the offline PC. 
   The following configuration must be active for the wallet to be in offline
   mode:  Settings->Options, Wallet tab: 
            Enable Offline Signing
              Sign (Offline mode)

   On the overview page, the mode is shown: Cold storage offline mode

   Click the Z-Sign button to open the sign page.
   A page similar to the 'Offline transaction signing' window will be
   displayed. The top text area accepts the transaction data. The bottom
   text area will contain the signed transaction. Paste the sign request
   data ('z_sign_offline') that you obtained from the online wallet into
   the 'Unsigned transaction input' box.
   Press the Sign button. If you have the private key (spending key) of
   the 'Pay From' address in your wallet the signed output will be 
   displayed in the 'Signed transaction output' box.
   Copy this text and store it in a text file and transport it back to
   the online machine.

2.3 Send the signed transaction (online wallet)
   Paste the signed data ('sendrawtransaction') that you obtained from
   the offline wallet into the 'Signed transaction input' box.
   Press the Send transaction button. The window will close.
   The transaction will be submitted to the pirate network for processing.
   On the Transaction page you'll see the transaction in the history
   with the network confirmations shown next to the entry.

   Congratulations, you've successfully created and send a payment
   using an offline wallet

3. Send coins from your wallet using the console
   To send funds require three actions:
     1. Construct the transaction on the online wallet.
     2. Authorise (sign) the transaction on the offline wallet.
     3. Submit the signed transaction to the network from the online wallet.
3.1 Construct the transaction (online wallet)
   Launch the wallet on the online PC
   Click on Help->Debug window. Select the 'Console' tab. In the bottom
   row of the console you enter the text commands to the application. All the
   actions available in the GUI and many more are available in this console. 
   If you type help<enter> a whole list of possible commands will be displayed.
   The particular command that we're interested in is 'z_sendmany_prepare_offline',
   'z_sign_offline' and 'sendrawtransaction'

   You'll need the following pieces of information before you start to construct
   a transaction:
   - The 'FROM_ADRES' - This is the address from which the funds will be taken. In
                        this example it is your viewing address starting with 'zs1'.
                        You can obtain it by opening the online wallet, click on
                        'Receive'. Obtain the address in the list. Right click on
                        the address and select 'Copy address'.
   - The 'RECIPIENT_ADRES' - This is the address that you are sending the coins to.
                        It can be another address in your own wallet that you use 
                        to test the transfer of funds, a deposit address on an 
                        exchange, or an address of a friend or shop that you want
                        to send coins to
   - AMOUNT - How many coins you want to send. You can specify payments not only up
              to 2 decimal places, traditionally reserved for cents, but up to 6 
              decimal places. You can therefore send milli (0.001) and micro (0.000001)
              coin payments. In the transaction text is written as value.fraction,
              i.e. 12.4 or 0.0024
   - MEMO   - The text memo is a description that you and the recipient can read 
              regarding the transaction. It can for instance contain an invoice 
              number for goods purchased.
   - CONFIRMATIONS - How many network confirmations must occur before the transaction
                     is deemed successful. It is safe to leave this at 1
   - FEE    - How many coins you want to pay the pirate network backoffice in fees
              for maintaining the pirate network. In the transaction text is 
              written as value.fraction, i.e. 0.0001

   It's best to construct the transaction in an external text editor and copy and
   paste the text into the wallet application. Take note of all the funny syntax
   characters, like [] {} "" and ''
   The text must not contain any extra spaces or newline characters.

   The syntax example shows in capital letters where you must substitute the 5
   transaction values:   
   z_sendmany_prepare_offline "FROM_ADRES" '[{"address":"RECIPIENT_ADRES","amount":AMOUNT,"memo":"MEMO"}]' CONFIRMATIONS FEE

   A populated example, with fake addresses shown for illustrative purposes:
   z_sendmany_prepare_offline "zs1examplepaymentaddresstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789bc" '[{"address":"zs1examplerecipientaddressvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789abc","amount":0.001,"memo":"Test memo"}]' 1 0.0001
   Once you've prepared your transaction, paste the full command into the console of
   the online wallet. If the wallet accepted the command, it will print the generated
   transaction in the console text window.

   The transaction data consists of approx. 2850 text characters. You need to copy the
   part after the "Success": status output: z_sign_offline 1 \"zs1w8ycdeeuyx...
   all the way up the last number, without the "double quotes" in which the whole thing
   is enclosed

   Paste the full text into a text editor and save it. The file containing the transaction
   need to be transferred to the offline machine. A USB memory stick is probably the
   easiest way to exchange the file between the online and offline machines.

3.2 Authorise the transaction (offline wallet)
   Launch the wallet on the offline PC

   Click on Help->Debug window. Select the 'Console' tab. In the bottom row of the console
   you can enter text commands to the application.

   Open the file containing the transaction data that was created on the online machine in
   step 3.1 above. Copy and paste the contents into the console. The command starts with:
   z_sign_offline and ends in some numbers. Do not include the "double quotes" in which
   the output was wrapped when it was created in the console windows of the online wallet.

   Wait for the transaction to be signed. The output is printed in the console window of
   the wallet. The output contains the instruction: "Instruction: Paste the contents into
   your on-line wallet without the \"\"":

   Paste from 'sendrawtransaction' up to the end of the message, excluding the "double 
   quotes"

   Paste the full text into a text editor and save it. The file containing the signed
   transaction need to be transferred to the online machine. A USB memory stick is 
   probably the easiest way to exchange the file between the offline and online machines.

3.3 Send the signed transaction (online wallet)
   On the online wallet, click on Help->Debug window. Select the 'Console' tab.

   Open the file containing the signed transaction data that was created on the offline
   machine in step 3.2 above. Copy and paste the contents into the console. The command 
   starts with: sendrawtransaction ....

   If the transaction is accepted it will be submitted to the pirate network for 
   processing.

   Close the debug window. In the main application, click on the 'Transactions' button.
   Your transaction should be in the top line of the transaction log. The progress icon
   will fill up to highlight the progress.

   Congratulations, you've successfully created and send a transaction using an offline
   wallet
