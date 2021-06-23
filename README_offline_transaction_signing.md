## Sign a transaction in an off-line wallet

## Setup
1. Two wallets are required: One running on a PC with internet access. This we'll
   call the 'on-line wallet'. The other on a PC without internet or network access,
   called the 'off-line wallet'. The internet & network disconnected machine will
   offer a physical barrier so that attackers cannot access the spending keys (private
   keys) in your wallet.

2. Start up the Pirate treasure chest wallet on the on-line machine. Let it finish
   synchronising with the network over the internet.
   The process can be speed up by first downloading the ARRR_bootstrap file and
   extracting it into the data directory of the application.
   On Linux its located in $HOME/.komodo/PIRATE under 'blocks' and 'chainstate'

   zcash parameters: approx 1.6 GB
   block chain: approx 9 GB

3. Copy the zcash parameters from the on-line PC to the
   off-line machine. On linux its located in $HOME/.zcash-params

4. First run of the off-line wallet
   Launch the wallet. The startup splash screen will be displayed,
   with the status bar showing 'Verifying sprout-verifying.key...' etc.

   A popup will appear, stating: 'New installation detected. Press OK
   to download the blockchain bootstrap'. Press 'Cancel'

   The main application page will be shown, with the progress window
   showing how many blockchain block must still be synchronised.
   Press 'Hide' to get it out of the way

   Close the application

5. Navigate to the wallet directory of Treasure chest
   On Linux: $HOME/.komodo/PIRATE
   Edit PIRATE.conf
   Add this entry: maxconnections=0
   Save the file
   This will prevent the off-line wallet from connecting to the network, even if the machine
   has internet access. It will also configure the wallet to run in off-line mode.


## Adreses
1. The philosophy of an off-line transaction
   Each address consists of a public and private part. The public part is what you share
   with people, so that they can make payments to you. The private part is required to
   spend the funds in that address. This should be kept private at all times. If somebody
   obtains your private key they can spend your funds. The network will process the
   transaction if the cryptographic signature is correct, regardless of where the
   transaction was send from. Your wallet.dat file stores your addresses. An attacker
   will try to obtain your wallet file in order to steal your private keys, and per
   extension, your funds. To prevent somebody from launching a remote attack on your
   computer over the internet or with mallware on your PC, we split up the keys.

   On PirateNetwork there is a further distinction for the public part of the address.
   Public address: (Starts with 'zs') This is what you share with somebody to pay you.
                   They can not obtain further information regarding the balance of that
                   address or the transaction history of the address.
   Extended viewing key: (Starts with 'zxviews') The extended viewing key contains the
                   public address and additional information enabling the wallet to show
                   you the current balance of the address and the transaction history of
                   the address. You use the extended viewing key in your own off-line
                   wallet to monitor the status of the address as well as to generate an
                   off-line sign request when you want to spend the funds in that address.
                   You do not normally share the extended viewing key with somebody, just
                   like the private key.

   The wallet on the off-line PC contains the full address, i.e. viewing (public) &
   spending (private). The wallet on the on-line PC only contains the viewing key.
   If the wallet with the viewing key is stolen its (almost) no deal - information
   is leaked regarding the balance & transaction history of your addresses. This is
   clearly not a desireable thing, but at least they will not be able to spend that
   balance, like they would have if the wallet contained the spending (private) part
   of the address as well.

2. Create an address in the off-line wallet
   Launch the off-line wallet. Once verification of the zcash parameters are
   completed the summary page is displayed. If you've set 'maxconnections=0' in
   PIRATE.conf, the wallet will report that its in Off-line mode, at the top of
   the screen.

   Click the 'Receive' button. The addresses on which you can receive funds will
   be displayed. The 'Mine' column should have a check mark next to it, which
   indicates that the wallet has the spending key (private key) of that address.
   During the first run of the application it will automatically create a
   wallet.dat file and generate one address.
   If you require a new address to be generated, you can click the 'New' button
   at the bottom of the window. A popup will appear that states 'New receiving z-address'.
   Click OK to complete the operation.

   You'll notice that the balance next to these addresses are always 0.000. The off-line
   PC cannot scan the blockchain to see if any funds were send to its addresses. You'll
   use the on-line PC to view the balance of your addresses.

3. Exporting an address to the on-line wallet
   You need to export the extended viewing key of the address from your off-line wallet
   to the on-line wallet. To perform this task, click on the 'Receive' botton to
   display all your addresses. Righ click on the desired address and select 'export
   extended viewing key'. A window with about 4 lines of text will be displayed,
   starting with 'zxviews....' Click with the mouse pointer on the text and press
   Ctrl+a to select all the text. Then copy and paste it into a text editor. Save the
   text file and transport to the on-line PC. Since the off-line machine is
   disconnected from all networks for security concerns, you'll probably use a
   USB memory stick to transport the file to the on-line machine.

   Note: The process contains more data than only the public address. You share the public address,
   starting with 'zs1...', with people so that they can send pirate coins to you. You use the extended
   viewing key to monitor the balance and transaction history of the address and to request off-line
   transaction signing for that address when you want to spend its funds. This will only be used in your
   on-line wallet.

4. Import extended viewing key in the on-line wallet
   On the on-line PC, open the file containing the extended viewing key.
   Copy the text of the viewing key. Launch the treasure chest application.
   Once it is operational, select File->Import viewing key
   A popup window will appear. Paste the text in the edit box and select OK
   The whole blockchain is scanned by the application to see if there were any funds send to that
   address in the past. This can take up to an hour on a PC with an Intel i5 processor.
   Once completed, click on the 'Receive' button. The address should be visible in the list.
   Note that there is no checkmark in the 'Mine' column. It indicates that
   you are only viewing the activity on the address and that the on-line wallet doesn't have the
   spending key (private key) to authorise a spending transaction for that address. This is exactly
   what we want to accomplish with the split wallet (on-line & off-line) philosophy.

## Transactions   
1. Fund the new address
   You'll first need some funds on the address before we can illustrate how you authorise payments with
   the off-line wallet. You'll need a friend to send you some pirate coins or you'll need to purchase some
   Pirate coin on an exchange, like TradeOgre, and send the coins to your address.

   If you're using a Treasure chest wallet app, click on the Z-Send tab. Select an address from the Pay From dropdown
   that has some funds in it. Enter your public address into the 'Pay To' edit box. This is your address starting with
   'zs1' and not the 'full viewing key' that starts with 'zxviews' Enter the amount, a text memo describing the
   transaction and the transaction fee for the miners.
   Click Send
   Click the Transactions button. You'll see the new transaction at the top of the list. Wait for the network to
   accept the transaction. The clock icon next to the transaction will show the number of network confirmations
   as the transaction is processed.

   If you're withdrawing the coins from an exchange, the exchange will prompt you to enter the address where you
   want the coins to be send to and how many of your coins on the exchange you want to transfer to the new
   address. Enter all the required information and submit the transaction. The exchange will provide you with
   the transaction id (txid) once the transaction is submitted to the network for processing.

   On your on-line wallet, click the 'Transactions' button. Once the network has processed the transaction and
   recorded it in the blockchain you'll see the transaction entry appear in your log. You can usually spend it
   after 6 confirmation.

2. Send coins from your wallet using the GUI
   To send funds require three actions:
     1. Construct the transaction on the on-line wallet.
     2. Authorise (sign) the transaction on the off-line wallet.
     3. Submit the signed transaction to the network from the on-line wallet.

2.1 Construct the transaction (on-line wallet)
   Launch the wallet on the on-line PC. On the overview page, the mode must
   indicate 'On-line'
   Click the Z-Send button to open the send page
   Fill in the fields:
   Pay From: Select an address that is marked 'Off-line'. Only addresses with a
             balance are shown.
   Pay To:   Fill in the recipient address
             If you want to send the funds to yourself, copy one of your own
             addresses from the 'Receive' page. The 'Pay From' and 'Pay To'
             addresses must differ.
   Amount:   The amount of coins to send.
   Memo:     Optionally a memo describing the transaction, like an invoice nr
   Transaction Fee: The amount of coin you want to pay the miners to process your transaction
                    Suggested fee is 100 microARRR (uARRR)
   Press the 'Prepare off-line transaction' button. The confirmation window appears. Press
   'Yes' to continue. The 'off-line transaction signing' window will appear. In the top
   text area the prepared transaction data will be displayed under the heading: 'Unsigned transaction'.
   Copy this text and store it in a text file and transport it to the off-line machine.
   A USB memory stick is probably the easiest way to exchange the file between the two machines.

2.2 Authorise the transaction (off-line wallet)
   Launch the wallet on the on-line PC. On the overview page, the mode must
   indicate 'Off-line'
   Click the Z-Sign button to open the sign page
   A page similar to the 'Off-line transaction signing' window will be displayed.
   The top text area accepts the transaction data. The bottom text area will contain
   the signed transaction. Paste the sign request data ('z_sign_offline') that you obtained
   from the on-line wallet into the 'Unsigned transaction input' box.
   Press the Sign button. If you have the private key (spending key) of the pay from address
   in your wallet the signed output will be displayed in the 'Signed transaction output' box.
   Copy this text and store it in a text file and transport it back to the on-line machine.

2.3 Send the signed transaction (on-line wallet)
   Paste the signed data ('sendrawtransaction') that you obtained
   from the off-line wallet into the 'Signed transaction input' box.
   Press the Send transaction button. The window will close. In the main wallet a
   result box will appear, that contains the transaction ID (txid).
   The transaction will be submitted to the pirate network for processing.

   Congratulations, you've successfully created and send a transaction using an off-line wallet


3. Send coins from your wallet using the console
   To send funds require three actions:
     1. Construct the transaction on the on-line wallet.
     2. Authorise (sign) the transaction on the off-line wallet.
     3. Submit the signed transaction to the network from the on-line wallet.
3.1 Construct the transaction (on-line wallet)
   Launch the wallet on the on-line PC from the command line. The transaction output is printed on the command line.
   If launched from the graphic desktop, i.e. the start button or toherwise, you might not get access to the command
   line output of the application.
   Click on Help->Debug window. Select the 'Console' tab. In the bottom row of the console
   you can enter text commands to the application. All the actions available in the GUI and many more are available in on this
   console. If you type help<enter> a whole list of possible commands will be displayed. The particular command that we're
   interested in is 'z_sendmany_prepare_off-line', 'z_sign_off-line' and 'sendrawtransaction'

   You'll need the following pieces of information before you start to construct a transaction:
   - The 'FROM_ADRES' - This is the address from which the funds will be taken. In this example it is your
                        viewing address starting with 'zs1'. You can obtain it by opening the on-line wallet,
                        click on 'Receive'. Obtain the address in the list. Right click on the address and select
                        'Copy address'.
   - The 'RECIPIENT_ADRES' - This is the address that you are sending the coins to. It can be another address in your
                             own wallet that you use to test the transfer of funds, a deposit address on an exchange,
                             or an address of a friend or shop that you want to send coins to
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
   z_sendmany_prepare_off-line "FROM_ADRES" '[{"address":"RECIPIENT_ADRES","amount":AMOUNT,"memo":"MEMO"}]' CONFIRMATIONS FEE

   A populated example, with fake addresses only shown for illustrative purposes:
   z_sendmany_prepare_off-line "zs1examplepaymentaddresstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789bc" '[{"address":"zs1examplerecipientaddressvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789abc","amount":0.001000,"memo":"Test memo"}]' 1 0.000010

   Once you've prepared your transaction, paste the full command into the console of the on-line wallet.
   If the wallet accepted the command, it will print the following result:
   "generating transaction": "Please wait for the transaction builder to finish. The output is on the shell. Look for the string: z_sign_off-line ..."

   Go the the console where you've launched the application from. The transaction output will be printed there. It starts with:
   "Paste the full contents into the console of your off-line wallet to sign the transaction:"
   The transaction data consists approx. of 2850 text characters. You need to copy the part after the instruction that starts with the
   command: z_sign_off-line....

   Paste the full text into a text editor and save it. The file containing the transaction need to be transferred to the off-line machine.
   A USB memory stick is probably the easiest way to exchange the file between the on-line and off-line machines.

3.2 Authorise the transaction (off-line wallet)
   Launch the wallet on the off-line PC from the command line.

   Click on Help->Debug window. Select the 'Console' tab. In the bottom row of the console you can enter text commands to
   the application.

   Open the file containing the transaction data that was created on the on-line machine in step 2.1 above.
   Copy and paste the contents into the console. The command starts with: z_sign_off-line ....

   Wait for the transaction to be signed.  The output is printed in the console window of the wallet. The output contains
   the instruction: "signed transaction: Paste into on-line wallet without the """
   Copy everything from 'sendrawtransaction' up to the end of the message, excluding the ""

   Paste the full text into a text editor and save it. The file containing the signed transaction need to be transferred to
   the on-line machine. A USB memory stick is probably the easiest way to exchange the file between the off-line and on-line machines.

3.3 Send the signed transaction (on-line wallet)
   On the on-line wallet, click on Help->Debug window. Select the 'Console' tab.

   Open the file containing the signed transaction data that was created on the off-line machine in step 3.2 above.
   Copy and paste the contents into the console. The command starts with: sendrawtransaction ....

   The transaction will be submitted to the pirate network for processing.

   Close the debug window. In the main application, click on the 'Transactions' button.
   Your transaction should be in the top line of the transaction log. The progress icon
   will fill up to highlight the progress.

   Congratulations, you've successfully created and send a transaction using an off-line wallet
