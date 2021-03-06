#include "cardHandler.h"
#include "pinout.h"

CardHandler::CardHandler() 
{    
    #if (defined MFRC522_CS) && (defined MFRC522_RST)
        m_pRfReader = new MFRC522(MFRC522_CS, MFRC522_RST);    
    #else
        m_pRfReader = NULL;
    #endif
}

CardHandler::CardHandler(MFRC522 *pCardReader) 
{        
    m_pRfReader = pCardReader;
}

CardHandler::~CardHandler()
{    
}


void CardHandler::connectCardReader(void)
{
    String myVersion;

    ESP_LOGD(TAG, "Connecting RFID reader...");

    m_pRfReader->PCD_Init();                // Init MFRC522

	// Get the MFRC522 firmware version
	byte v = m_pRfReader->PCD_ReadRegister(MFRC522::VersionReg);

	// Lookup which version
	switch(v) {
		case 0x88: myVersion = String("(clone)");            break;
		case 0x90: myVersion = String("v0.0");               break;
		case 0x91: myVersion = String("v1.0");               break;
		case 0x92: myVersion = String("v2.0");               break;
		case 0x12: myVersion = String("counterfeit chip");   break;
		default:   myVersion = String("(unknown)");
	}

    ESP_LOGI(TAG, "Firmware Version: 0x%02x = %s", v, myVersion.c_str());
	

	// When 0x00 or 0xFF is returned, communication probably failed
	if ((v == 0x00) || (v == 0xFF))
    {
	    ESP_LOGW(TAG, "WARNING: Communication failure, is the MFRC522 properly connected?");
    }
}


bool CardHandler::IsNewCardPresent( void )
{
    bool result = false;

    //make sure we are attached to a RFID reader
    if (m_pRfReader)
    {

        // Look for new card
        if ( m_pRfReader->PICC_IsNewCardPresent() == true) 
        {
            result = true;
        }
    } 

    //some simple debug test data    
    else 
    {
        result = true;
    }

    return result;    
}


bool CardHandler::IsCardPresent( CardSerialNumber *pActualCardSerial )
{
    bool result = false;

    //make sure we are attached to a RFID reader
    if (m_pRfReader)
    {
        // Since wireless communication is voodoo we'll give it a few retrys before killing the music
        for (uint32_t counter = 0; counter < 3; counter++) 
        {
            // Detect Tag without looking for collisions
            byte bufferATQA[2];
            byte bufferSize = sizeof(bufferATQA);

            MFRC522::StatusCode status = m_pRfReader->PICC_WakeupA(bufferATQA, &bufferSize);

            if (status == MFRC522::STATUS_OK)
            {
                if (m_pRfReader->PICC_ReadCardSerial()) 
                {
                    bool uidEqual = true;

                    //compare if the Uids have the same size
                    if (pActualCardSerial->SerialNumberLength == m_pRfReader->uid.size)
                    {
                        //check the diffferent bytes
                        for (uint32_t counter=0; counter < pActualCardSerial->SerialNumberLength; counter++)
                        {
                            if (m_pRfReader->uid.uidByte[counter] != pActualCardSerial->SerialNumber[counter])
                            {
                                uidEqual = false;
                                break;
                            }
                        }

                        if (uidEqual == true) 
                        {
                            result = true;
                            break;
                        }
                    }
                }
            }
        } // "magic loop"
    }

    return result;    
}


bool CardHandler::GetCardSerial(CardSerialNumber *pActualCardSerial)
{
    bool result = false;
    
    //make sure we are attached to a RFID reader
    if (m_pRfReader)
    {
        // Read the serial of one card
        if ( m_pRfReader->PICC_ReadCardSerial()) 
        {
            memset(pActualCardSerial->SerialNumber, 0, sizeof(pActualCardSerial->SerialNumber));

            pActualCardSerial->SerialNumberLength = m_pRfReader->uid.size;
            memcpy(pActualCardSerial->SerialNumber, m_pRfReader->uid.uidByte, pActualCardSerial->SerialNumberLength );

            result = true;
        }
    }

    //some simple debug test data
    // else 
    // {        
    //     memset(pActualCardSerial->SerialNumber, 0, sizeof(pActualCardSerial->SerialNumber));

    //     pActualCardSerial->SerialNumberLength = 4;
    //     pActualCardSerial->SerialNumber[0] = 15;
    //     pActualCardSerial->SerialNumber[1] = 16;
    //     pActualCardSerial->SerialNumber[2] = 17;
    //     pActualCardSerial->SerialNumber[3] = 18;

    //     result = true;
    // }

    return result;
}


bool CardHandler::ReadCardInformation(CardData *pTarget)
{
    bool result = false;

    //we start with am invalid result
    pTarget->m_valid = false;

    if (m_pRfReader != NULL)
    {
        MFRC522::StatusCode     status;
        MFRC522::PICC_Type      piccType;

        CardDataBlock_s         cardDataBlock;

        uint8_t                 startBlockNumber;
        uint32_t                cardBlockSize;          //the number of bytes we could read from every block

        uint32_t                pagesNeeded;
        uint8_t                 *pDataTarget;

        uint8_t                 buffer[18];
        uint8_t                 bufferSize = sizeof(buffer);

        char                    *pFileName = NULL;

        //check if the union has the correct size (compare uint8_t array and structure)
        if (sizeof(cardDataBlock.Entry) != sizeof(cardDataBlock.Raw))
        {
            ESP_LOGE(TAG, "Information structure size is not equal to the readout array! (%u instead of %u)", sizeof(cardDataBlock.Entry), sizeof(cardDataBlock.Raw));
            goto FinishReadInformation;
        }

        //compare the input structure with the expected size
        if (sizeof(cardDataBlock.Raw) != INFORMATION_BLOCK_SIZE) 
        {
            ESP_LOGE(TAG, "Information structure has the wrong size! (%u instead of %u)", sizeof(cardDataBlock.Raw), INFORMATION_BLOCK_SIZE);
            goto FinishReadInformation;
        }

        //prepare the default key
        for (uint32_t counter=0; counter < sizeof(m_MFRC522Key.keyByte); counter++)
        {
            m_MFRC522Key.keyByte[counter] = 0xFF;
        }

        //get the type for the card
        piccType = m_pRfReader->PICC_GetType(m_pRfReader->uid.sak);
        ESP_LOGD(TAG, "PICC type: %s", m_pRfReader->PICC_GetTypeName(piccType));

        if ((piccType != MFRC522::PICC_TYPE_MIFARE_MINI ) &&
            (piccType != MFRC522::PICC_TYPE_MIFARE_1K ) &&
            (piccType != MFRC522::PICC_TYPE_MIFARE_4K ) &&
            (piccType != MFRC522::PICC_TYPE_MIFARE_UL ) )
        {
            ESP_LOGW(TAG, "Unsupported card type deteced");
            goto FinishReadInformation;
        }

        //authentificate with the card
        if ((piccType == MFRC522::PICC_TYPE_MIFARE_MINI ) ||
            (piccType == MFRC522::PICC_TYPE_MIFARE_1K ) ||
            (piccType == MFRC522::PICC_TYPE_MIFARE_4K ) )
        {
            //initiate the variables for this card type
            startBlockNumber = INFORMATION_BLOCK_MIFARE_1K;

            cardBlockSize   = 16;

            // Authenticate using key A
            ESP_LOGV(TAG, "Authenticating MIFARE Mini/1k/4k using key A...");
            status = m_pRfReader->PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, startBlockNumber, &m_MFRC522Key, &(m_pRfReader->uid));
        } 
        else if (piccType == MFRC522::PICC_TYPE_MIFARE_UL ) 
        {
            byte pACK[] = {0, 0}; //16 bit PassWord ACK returned by the NFCtag

            startBlockNumber = INFORMATION_BLOCK_MIFARE_ULTRA;
            
            cardBlockSize     = 4; 

            // Authenticate using key A
            ESP_LOGV(TAG, "Authenticating MIFARE UL using key A...");
            status = m_pRfReader->PCD_NTAG216_AUTH(m_MFRC522Key.keyByte, pACK);
        }

        //check the authentification result
        if (status != MFRC522::STATUS_OK) {
            ESP_LOGW(TAG, "Card Authentification failed: %s", m_pRfReader->GetStatusCodeName(status));
            goto FinishReadInformation;
        }

        // read information block(s)
        pagesNeeded  = INFORMATION_BLOCK_SIZE / cardBlockSize;
        pDataTarget  = &(cardDataBlock.Raw[0]);

        ESP_LOGV(TAG, "Reading data from %u block(s) starting at block %u", pagesNeeded, startBlockNumber);

        for (uint32_t block = startBlockNumber; block < (startBlockNumber + pagesNeeded); block++ )
        {
            status = m_pRfReader->MIFARE_Read(block, buffer, &bufferSize);
            if (status != MFRC522::STATUS_OK) 
            {
                ESP_LOGW(TAG, "MIFARE_Read() failed: %s", m_pRfReader->GetStatusCodeName(status));
                goto FinishReadInformation;
            }

            //reading was successfull, we could copy the data into our own structure
            memcpy(pDataTarget, buffer, cardBlockSize);
            pDataTarget += cardBlockSize;        
        }

        //DumpByteArray("Data from card: ", cardDataBlock.Raw, sizeof(cardDataBlock.Raw));

        if (cardDataBlock.Entry.Header.Cookie != INFORMATION_BLOCK__MAGIC_KEY) 
        {
            ESP_LOGI(TAG, "Wrong Magic Key (%08x), card is not for this box", cardDataBlock.Entry.Header.Cookie);
            goto FinishReadInformation;
        }


        //check the header version
        ESP_LOGI(TAG, "Card Version %u found:", cardDataBlock.Entry.Header.Version );

        if (cardDataBlock.Entry.Header.Version == 1)
        {
            uint8_t     activeKeyBlock = 0;
            uint8_t     newKeyBlock = TARGET_BLOCK_MIFARE_1K;

            uint16_t    position = 0;


            // handle configuration
            pTarget->m_Resumeable           = cardDataBlock.Entry.MetaData.Configuration.Resumeable?true:false;
            pTarget->m_Volume               = cardDataBlock.Entry.MetaData.Volume;

            //get the file string
            //reserve some memory for the string and fill it with '0'
            pFileName = (char *) malloc((cardDataBlock.Entry.MetaData.FileNameLength + 1) * sizeof(char));

            if (pFileName == NULL)
            {
                ESP_LOGE(TAG, "Could not allocate memory for file-name.");
                goto FinishReadInformation;
            }

            memset(pFileName, 0, (cardDataBlock.Entry.MetaData.FileNameLength + 1) * sizeof(char));

            if ((piccType == MFRC522::PICC_TYPE_MIFARE_MINI ) ||
                (piccType == MFRC522::PICC_TYPE_MIFARE_1K ) ||
                (piccType == MFRC522::PICC_TYPE_MIFARE_4K ) )
            {
                startBlockNumber = TARGET_BLOCK_MIFARE_1K;
            } 
            else if (piccType == MFRC522::PICC_TYPE_MIFARE_UL ) 
            {
                startBlockNumber = TARGET_BLOCK_MIFARE_ULTRA;
            }
            
            //check how many bytes we need
            if (cardDataBlock.Entry.MetaData.FileNameLength == 0)
            {
                ESP_LOGE(TAG, "String length of ZERO given!");
                goto FinishReadInformation;
            }
            else if (cardDataBlock.Entry.MetaData.FileNameLength < cardBlockSize) 
            {
                cardBlockSize = cardDataBlock.Entry.MetaData.FileNameLength;
            } 

            while (position < cardDataBlock.Entry.MetaData.FileNameLength)
            {
                // additional authentification is only neccessary on "classic" cards
                if ((piccType == MFRC522::PICC_TYPE_MIFARE_MINI ) ||
                    (piccType == MFRC522::PICC_TYPE_MIFARE_1K ) ||
                    (piccType == MFRC522::PICC_TYPE_MIFARE_4K ) )
                {
                 
                    newKeyBlock = (startBlockNumber / 4) * 4 + 3;

                    //authentificate the block
                    if (newKeyBlock != activeKeyBlock) 
                    {
                        ESP_LOGV(TAG, "Authenticating using key A...");
                        status = m_pRfReader->PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, newKeyBlock, &m_MFRC522Key, &(m_pRfReader->uid));
                        if (status != MFRC522::STATUS_OK) {
                            ESP_LOGW(TAG, "Authentication failed for block %u: %s", newKeyBlock, m_pRfReader->GetStatusCodeName(status));
                            goto FinishReadInformation;
                        }

                        activeKeyBlock = newKeyBlock;
                    }
                }

                memset(buffer, 0, sizeof(buffer));

                // read block
                status = m_pRfReader->MIFARE_Read(startBlockNumber, buffer, &bufferSize);
                if (status != MFRC522::STATUS_OK) 
                {
                    ESP_LOGW(TAG, "MIFARE_Read() failed on block %u: %s", startBlockNumber, m_pRfReader->GetStatusCodeName(status));
                    goto FinishReadInformation;
                }

                buffer[16] = 0;
                ESP_LOGV(TAG, "Read block %u content \"%s\"", startBlockNumber, buffer);

                //reading was successfull, we could copy the data into our own structure
                strncat(pFileName, (const char*)buffer, cardBlockSize);

                //remember the number of byte read
                position += cardBlockSize;

                startBlockNumber++;

                // additional authentification is only neccessary on "classic" cards
                if ((piccType == MFRC522::PICC_TYPE_MIFARE_MINI ) ||
                    (piccType == MFRC522::PICC_TYPE_MIFARE_1K ) ||
                    (piccType == MFRC522::PICC_TYPE_MIFARE_4K ) )
                {
                    // make sure the next block is not a key block
                    if ((startBlockNumber > 2) && (((startBlockNumber - 3) % 4) == 0 ))
                    {
                        startBlockNumber++;
                    }
                }
            } // read out loop
            
            // coyp the string into the result structure
            pTarget->m_fileName = String(pFileName);

            ESP_LOGV(TAG, "Read target String: \"%s\"", pTarget->m_fileName.c_str());

            pTarget->m_valid = true;
        }
        else 
        {
            ESP_LOGW(TAG, "Unknown information version");
            result = false;
            goto FinishReadInformation;
        }

        // finish the read
        result = true;

        FinishReadInformation:

            //make sure we clear the file name buffer
            if (pFileName != NULL)
            {
                free(pFileName);
                pFileName = NULL;
            }
    }

    //create some dummy data for testing purpose
    // else
    // {
    //     pTarget->m_fileName = "/00.mp3";

    //     result = true;
    // }

    return result;
}

bool CardHandler::WriteCardInformation(CardData *pSource, CardSerialNumber *pActualCardSerial) 
{
    bool result = false;

    if (m_pRfReader != NULL)
    {
        CardDataBlock_s         cardDataBlock;

        MFRC522::StatusCode     status;
        MFRC522::PICC_Type      piccType;

        uint32_t                actualAuthentificatedSector;

        uint8_t                 operatingBlock;     //the block we are actual working with
        uint8_t                 operatingSector;    //the sector we are actual working with

        uint32_t                blockSize;          //the number of bytes we could read from every block
        uint32_t                sectorSize;         //

        uint32_t                neededBlocks;        // the number of blocks we must write before all data is stored
        uint8_t                 *pDataSource;
        uint8_t                 *pBuffer;

        uint32_t                position = 0;

        uint8_t                 buffer[18];
        uint32_t                useableBufferSize = 16;

        //check if the union has the correct size (compare uint8_t array and structure)
        if (sizeof(cardDataBlock.Entry) != sizeof(cardDataBlock.Raw))
        {
            ESP_LOGE(TAG, "Information structure size is not equal to the readout array! (%u instead of %u)", sizeof(cardDataBlock.Entry), sizeof(cardDataBlock.Raw));
            goto FinishWriteInformation;
        }

        //compare the input structure with the expected size
        if (sizeof(cardDataBlock.Raw) != INFORMATION_BLOCK_SIZE) 
        {
            ESP_LOGE(TAG, "Information structure has the wrong size! (%u instead of %u)", sizeof(cardDataBlock.Raw), INFORMATION_BLOCK_SIZE);
            goto FinishWriteInformation;
        }

        //prepare the default key
        for (uint32_t counter=0; counter < sizeof(m_MFRC522Key.keyByte); counter++)
        {
            m_MFRC522Key.keyByte[counter] = 0xFF;
        }

        //check if the "known" card is still there (this will also wake up the card)
        if (!IsCardPresent(pActualCardSerial)) 
        {
            goto FinishWriteInformation;
        }

        //prepare the internal strucure
        cardDataBlock.Entry.Header.Cookie                       = INFORMATION_BLOCK__MAGIC_KEY;
        cardDataBlock.Entry.Header.Version                      = 1;

        cardDataBlock.Entry.MetaData.Volume                     = pSource->m_Volume;
        cardDataBlock.Entry.MetaData.Configuration.Full         = 0;
        cardDataBlock.Entry.MetaData.Configuration.Resumeable   = pSource->m_Resumeable?1:0;        

        cardDataBlock.Entry.MetaData.FileNameLength             = pSource->m_fileName.length();

        //limit file name length to 255 characters
        if (cardDataBlock.Entry.MetaData.FileNameLength > 256)
        {
            ESP_LOGE(TAG, "Maximum supported File Name legth is 256 (%u)", cardDataBlock.Entry.MetaData.FileNameLength);
            goto FinishWriteInformation;
        }

        //clear the data buffer
        memset(buffer, 0, sizeof(buffer));

        //get the type for the card
        piccType = m_pRfReader->PICC_GetType(m_pRfReader->uid.sak);
        ESP_LOGD(TAG, "PICC type: %s", m_pRfReader->PICC_GetTypeName(piccType));

        if ((piccType != MFRC522::PICC_TYPE_MIFARE_MINI ) &&
            (piccType != MFRC522::PICC_TYPE_MIFARE_1K ) &&
            (piccType != MFRC522::PICC_TYPE_MIFARE_4K ) &&
            (piccType != MFRC522::PICC_TYPE_MIFARE_UL ) )
        {
            ESP_LOGW(TAG, "Unsupported card type deteced");
            goto FinishWriteInformation;
        }

        //authentificate with the card and set card specific parameters
        if ((piccType == MFRC522::PICC_TYPE_MIFARE_MINI ) ||
            (piccType == MFRC522::PICC_TYPE_MIFARE_1K ) ||
            (piccType == MFRC522::PICC_TYPE_MIFARE_4K ) )
        {
            //initiate the variables for this card type
            blockSize           = 16;
            sectorSize          = 4;

            operatingBlock      = INFORMATION_BLOCK_MIFARE_1K % sectorSize;
            operatingSector     = INFORMATION_BLOCK_MIFARE_1K / sectorSize;            

            pBuffer = &(buffer[0]);

            // Authenticate using key A
            ESP_LOGV(TAG, "Authenticating MIFARE Mini/1k/4k using key A...");
            status = m_pRfReader->PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, (operatingSector * sectorSize) + 3, &m_MFRC522Key, &(m_pRfReader->uid));
        } 
        else if (piccType == MFRC522::PICC_TYPE_MIFARE_UL ) 
        {
            byte pACK[] = {0, 0}; //16 bit PassWord ACK returned by the NFCtag

            blockSize           = 4;
            sectorSize          = 0x86;

            operatingBlock      = INFORMATION_BLOCK_MIFARE_ULTRA;
            operatingSector     = 0;
            
            pBuffer = &(buffer[0]);

            // Authenticate using key A
            ESP_LOGV(TAG, "Authenticating MIFARE UL using key A...");
            status = m_pRfReader->PCD_NTAG216_AUTH(m_MFRC522Key.keyByte, pACK);
        }

        if (status != MFRC522::STATUS_OK) 
        {
            ESP_LOGW(TAG, "Authenticating failed: %s", m_pRfReader->GetStatusCodeName(status));
            goto FinishWriteInformation;
        }

        
        neededBlocks  = INFORMATION_BLOCK_SIZE / blockSize;
        if (INFORMATION_BLOCK_SIZE % blockSize)
        {
            neededBlocks++;
        }

        pDataSource  = &(cardDataBlock.Raw[0]);

        ESP_LOGV(TAG, "Writing information to %u blocks starting at block %u", neededBlocks, operatingSector * operatingBlock);

        do 
        {            
            //copy the next data portion into the output buffer
            memcpy(pBuffer, pDataSource, blockSize);

            status = m_pRfReader->MIFARE_Write((operatingSector * sectorSize) + operatingBlock, buffer, useableBufferSize);
            if (status != MFRC522::STATUS_OK) 
            {
                ESP_LOGW(TAG, "MIFARE_Write() failed: %s", m_pRfReader->GetStatusCodeName(status));
                goto FinishWriteInformation;
            }

            //writing was successfull, advance the source pointer 
            pDataSource += blockSize;

            //look for the next block were we could store our data
            operatingBlock++;
            if (operatingBlock > (sectorSize-1))
            {
                operatingBlock = 0;
                operatingSector++;
            }

            neededBlocks--;

        } while ( neededBlocks > 0);
        
        ESP_LOGD(TAG, "Wrote information block to card");


        //write the file name
        //----------------------------------------------------
        if ((piccType == MFRC522::PICC_TYPE_MIFARE_MINI ) ||
            (piccType == MFRC522::PICC_TYPE_MIFARE_1K ) ||
            (piccType == MFRC522::PICC_TYPE_MIFARE_4K ) )
        {
            //initiate the variables for this card type
            blockSize           = 16;
            sectorSize          =  4;

            operatingBlock      = TARGET_BLOCK_MIFARE_1K % sectorSize;
            operatingSector     = TARGET_BLOCK_MIFARE_1K / sectorSize;  

            pBuffer = &(buffer[0]);

        } 
        else if (piccType == MFRC522::PICC_TYPE_MIFARE_UL ) 
        {
            blockSize           =  4;
            sectorSize          =  0x86;

            operatingBlock      = TARGET_BLOCK_MIFARE_ULTRA;
            operatingSector     = 0;
            
            

            pBuffer = &(buffer[0]);
        }

        actualAuthentificatedSector  = 0;

        neededBlocks  = (pSource->m_fileName.length()+1) / blockSize;

        if ((pSource->m_fileName.length()+1) % blockSize)
        {
            neededBlocks++;
        }

        ESP_LOGV(TAG, "Writing information to %u blocks(s) starting at block %u", neededBlocks, operatingSector * sectorSize + operatingBlock);

        //repeat until all block are written
        do 
        {   
            // check if we need to authentificate the sector
            if ((piccType == MFRC522::PICC_TYPE_MIFARE_MINI ) ||
                (piccType == MFRC522::PICC_TYPE_MIFARE_1K ) ||
                (piccType == MFRC522::PICC_TYPE_MIFARE_4K ) )
            {

                //authentificate the sector
                if (actualAuthentificatedSector != operatingSector) 
                {
                    ESP_LOGV(TAG, "Authenticating sector %u using key A...", operatingSector);
                    status = m_pRfReader->PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, (operatingSector * sectorSize + (sectorSize-1)), &m_MFRC522Key, &(m_pRfReader->uid));
                    if (status != MFRC522::STATUS_OK) {
                        ESP_LOGW(TAG, "Authentication failed for sector %u: %s", operatingSector, m_pRfReader->GetStatusCodeName(status));
                        goto FinishWriteInformation;
                    }

                    actualAuthentificatedSector = operatingSector;
                }
            }

            memset(buffer, 0, sizeof(buffer));

            // get always one byte more since the result is '\0' terminated
            pSource->m_fileName.getBytes(pBuffer, (blockSize + 1), position);

            ESP_LOGV(TAG, "writing information to sector %u block %u -> \"%s\"", operatingSector, operatingBlock, pBuffer);

            status = m_pRfReader->MIFARE_Write(operatingSector * sectorSize + operatingBlock, buffer, useableBufferSize);
            if (status != MFRC522::STATUS_OK) 
            {
                ESP_LOGW(TAG, "MIFARE_Write(%u,%u) failed: %s", operatingSector, operatingBlock, m_pRfReader->GetStatusCodeName(status));
                goto FinishWriteInformation;
            }


            //look for the next block were we could store our data
            position += blockSize;

            operatingBlock++;
            if (operatingBlock > (sectorSize-1))
            {
                operatingBlock = 0;
                operatingSector++;
            }

            neededBlocks--;

        } while (neededBlocks > 0);

        ESP_LOGI(TAG, "writing information ok");

        result = true;
        
    }

    FinishWriteInformation:
    
    StopCommunication();

    return result;
 }



void CardHandler::StopCommunication(void)
{
    //end communication with the card
    m_pRfReader->PICC_HaltA();
    m_pRfReader->PCD_StopCrypto1();
}

