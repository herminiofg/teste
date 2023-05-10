/* 
   (c) 2019 Microchip Technology Inc. and its subsidiaries.

   Subject to your compliance with these terms, you may use Microchip software and any
   derivatives exclusively with Microchip products. It is your responsibility to comply with third party
   license terms applicable to your use of third party software (including open source software) that
   may accompany Microchip software.

   THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
   EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY
   IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS
   FOR A PARTICULAR PURPOSE.

   IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
   INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
   WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP
   HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO
   THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL
   CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT
   OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS
   SOFTWARE.
 */

#include <stdint.h>
#include <string.h>
#include "mem_model.h"
#include "parser.h"
#include "lexer.h"

#define SOFT_STACK_SIZE JSON_MAX_KEYS

#define ASSERT_TOKEN(tok)                       \
	if(LEXER_OK != LEXER_nextToken((tok)))      \
	{										    \
		return PARSER_BAD_FORMAT;               \
	}                                           \

#define ASSERT_NODE_ALLOC(node)                 \
	(node) = MEM_MODEL_allocNode();             \
	if(NULL == (node))                          \
	{                                           \
		return PARSER_NO_MORE_JSON_SPACE;       \
	}                                           \

#define ASSERT_AVAILABLE_SOFT_STACK             \
    if(stackTop >= SOFT_STACK_SIZE)             \
    {                                           \
        return PARSER_NO_MORE_JSON_SPACE;       \
    }                                           \


typedef struct {
    enum {
        CALL,
        CALL_LOOP,
        RESUME_LOOP,
    } type;

    struct {
        uint8_t root;
        jsonNode_t *currentNode;
    } data;
} recursionStack_t;

static recursionStack_t STACK[SOFT_STACK_SIZE];
static uint8_t stackTop;

parserStatus_t PARSER_parseTokens(void)
{
    parserStatus_t retVal = PARSER_OK;

    jsonToken_t nextToken;
    jsonNode_t *currentNode = NULL;
    jsonNode_t *previousNode = NULL;

    ASSERT_AVAILABLE_SOFT_STACK;
    STACK[stackTop++] = (recursionStack_t){
        .type = CALL,
        .data =
        {
            .root = 1,
        }
    };

    while (stackTop > 0)
    {
        recursionStack_t pop = STACK[--stackTop];

        switch (pop.type)
        {
            case CALL:
                ASSERT_TOKEN(&nextToken);
                if (pop.data.root && nextToken.type != LEFT_BRACE)
                {
                    retVal = PARSER_BAD_FORMAT; //Root must be an object
                    continue;
                }

                if (nextToken.type == LEFT_BRACE)
                {
                    ASSERT_NODE_ALLOC(currentNode);

                    if (previousNode != NULL)
                    {
                        previousNode->type = OBJECT;
                        previousNode->v = currentNode;
                    }

                    ASSERT_TOKEN(&nextToken);
                    if (RIGHT_BRACE == nextToken.type)
                    {
                        retVal = PARSER_OK;
                        continue;
                    }

                    ASSERT_AVAILABLE_SOFT_STACK;
                    STACK[stackTop++] = (recursionStack_t){
                        .type = CALL_LOOP,
                        .data =
                        {
                            .root = 0,
                        }
                    };
                }
                else
                {
                    retVal = LEAF_TOKEN;
                    continue;
                }

                break;

            case CALL_LOOP:

                if (STRING != nextToken.type)
                {
                    retVal = PARSER_BAD_FORMAT; // key must be string
                    continue;
                }
                currentNode->key = nextToken.value.str;

                ASSERT_TOKEN(&nextToken);
                if (COLON != nextToken.type)
                {
                    retVal = PARSER_BAD_FORMAT; // expecting colon after key
                    continue;
                }

                previousNode = currentNode;

                ASSERT_AVAILABLE_SOFT_STACK;
                STACK[stackTop++] = (recursionStack_t){
                    .type = RESUME_LOOP,
                    .data =
                    {
                        .currentNode = currentNode
                    }
                };

                ASSERT_AVAILABLE_SOFT_STACK;
                STACK[stackTop++] = (recursionStack_t){
                    .type = CALL,
                    .data =
                    {
                        .root = 0,
                    }
                };

                break;

            case RESUME_LOOP:
                if (retVal == LEAF_TOKEN)
                {
                    memcpy(&currentNode->value, &nextToken.value, sizeof (currentNode->value));
                }
                else if (retVal != PARSER_OK)
                {
                    retVal = PARSER_BAD_FORMAT;
                    continue;
                }

                ASSERT_TOKEN(&nextToken);
                if (RIGHT_BRACE == nextToken.type)
                {
                    retVal = PARSER_OK;
                    continue;
                }
                else if (COMMA != nextToken.type)
                {
                    retVal = PARSER_BAD_FORMAT; // expecting comma
                    continue;
                }

                ASSERT_NODE_ALLOC(pop.data.currentNode->h);
                previousNode = pop.data.currentNode;
                currentNode = pop.data.currentNode->h;

                ASSERT_TOKEN(&nextToken);

                ASSERT_AVAILABLE_SOFT_STACK;
                STACK[stackTop++] = (recursionStack_t){
                    .type = CALL_LOOP
                };

                break;

            default:
                break;
        }
    }

    return retVal;
}

//********************** abaixo linhas a mais **************************

typedef struct jsonNode {
    char *key;

    enum {
        LEAF,
        OBJECT,
    } type;

    union {
        char *str;
        int num;
    } value;
    struct jsonNode *h;
    struct jsonNode *v;
} jsonNode_t;

typedef enum {
    NONE,
    STRING,
    NUMBER,
    LEFT_BRACE,
    RIGHT_BRACE,
    COLON,
    COMMA,
} tokenType_t;

typedef struct {
    tokenType_t type;

    union {
        char *str;
        int num;
    } value;
} jsonToken_t;

jsonNode_t theJson[JSON_MAX_KEYS];

/********/

static partialLexStatus_t lex_number(jsonToken_t *token)
{
    char *numEnd = current_possition;
    char temp;

    for (uint8_t i = 0; i < strlen(current_possition); i++)
    {
        if (!strchr(NUMBER_CHARACTERS, current_possition[i]))
        {
            break;
        }
        numEnd = numEnd + 1;
    }

    // Only decimal numbers allowed

    if (numEnd - current_possition >= 1)
    {
        temp = *numEnd;
        *numEnd = 0;

        token->type = NUMBER;
        token->value.num = atoi(current_possition);

        *numEnd = temp;
        current_possition = numEnd;

        return LEXER_OK;
    }

    return NOT_FOUND;
}

static partialLexStatus_t lex_string(jsonToken_t *token)
{
    if (*current_possition == '"')
    {
        current_possition = current_possition + 1;
    }
    else
    {
        return NOT_FOUND;
    }

    for (uint8_t i = 0; i < strlen(current_possition); i++)
    {
        if (current_possition[i] == '"')
        {
            token->type = STRING;
            token->value.str = current_possition;

            current_possition[i] = 0;
            current_possition = current_possition + i + 1;
            return LEXER_OK;
        }
    }

    return NOT_FOUND;
}


static lexStatus_t lex_core(jsonToken_t *token)
{
    partialLexStatus_t ret;

    if (strlen(current_possition))
    {
        ret = lex_string(token);
        if (FOUND == ret)
        {
            return LEXER_OK;
        }
        else if (ret != NOT_FOUND)
        {
            return LEXER_BAD_FORMAT;
        }

        ret = lex_number(token);
        if (FOUND == ret)
        {
            return LEXER_OK;
        }
        else if (ret != NOT_FOUND)
        {
            return LEXER_BAD_FORMAT;
        }

        switch (current_possition[0])
        {
            case ' ': // intended fall through
            case '\\':
                current_possition = current_possition + 1;
                return LEXER_SKIP;

                break;

            case '{':
                token->type = LEFT_BRACE;
                break;

            case '}':
                token->type = RIGHT_BRACE;
                break;

            case ',':
                token->type = COMMA;
                break;

            case ':':
                token->type = COLON;
                break;

            default:
                return LEXER_BAD_FORMAT;
        }

        current_possition = current_possition + 1;

        return LEXER_OK;
    }

    return LEXER_NO_MORE_TOKENS;
}

lexStatus_t LEXER_nextToken(jsonToken_t *token)
{
    lexStatus_t ret;
    ret = lex_core(token);

    while (LEXER_SKIP == ret)
    {
        ret = lex_core(token);
    }

    return ret;
}


#define ASSERT_TOKEN(tok)                       \
	if(LEXER_OK != LEXER_nextToken((tok)))      \
	{										    \
		return PARSER_BAD_FORMAT;               \
	}                                           \


typedef struct {
    enum {
        CALL,
        CALL_LOOP,
        RESUME_LOOP,
    } type;

    struct {
        uint8_t root;
        jsonNode_t *currentNode;
    } data;
} recursionStack_t;

static recursionStack_t STACK[SOFT_STACK_SIZE];

/********************ssss2*/

jsonNode_t* MEM_MODEL_allocNode(void)
{
    if (theJsonTop < JSON_MAX_KEYS)
    {
        jsonNode_t *node = &theJson[theJsonTop];
        theJsonTop = theJsonTop + 1;
        return node;
    }
    else
    {
        return 0;
    }
}
/********************ssss3*/
#define ASSERT_AVAILABLE_SOFT_STACK             \
    if(stackTop >= SOFT_STACK_SIZE)             \
    {                                           \
        return PARSER_NO_MORE_JSON_SPACE;       \
    }   


#define ASSERT_NODE_ALLOC(node)                 \
	(node) = MEM_MODEL_allocNode();             \
	if(NULL == (node))                          \
	{                                           \
		return PARSER_NO_MORE_JSON_SPACE;       \
	}  

parserStatus_t PARSER_parseTokens(void)
{
    parserStatus_t retVal = PARSER_OK;

    jsonToken_t nextToken;
    jsonNode_t *currentNode = NULL;
    jsonNode_t *previousNode = NULL;

    ASSERT_AVAILABLE_SOFT_STACK;
    STACK[stackTop++] = (recursionStack_t){
        .type = CALL,
        .data =
        {
            .root = 1,
        }
    };

    while (stackTop > 0)
    {
        recursionStack_t pop = STACK[--stackTop];

        switch (pop.type)
        {
            case CALL:
                ASSERT_TOKEN(&nextToken);
                if (pop.data.root && nextToken.type != LEFT_BRACE)
                {
                    retVal = PARSER_BAD_FORMAT; //Root must be an object
                    continue;
                }

                if (nextToken.type == LEFT_BRACE)
                {
                    ASSERT_NODE_ALLOC(currentNode);

                    if (previousNode != NULL)
                    {
                        previousNode->type = OBJECT;
                        previousNode->v = currentNode;
                    }

                    ASSERT_TOKEN(&nextToken);
                    if (RIGHT_BRACE == nextToken.type)
                    {
                        retVal = PARSER_OK;
                        continue;
                    }

                    ASSERT_AVAILABLE_SOFT_STACK;
                    STACK[stackTop++] = (recursionStack_t){
                        .type = CALL_LOOP,
                        .data =
                        {
                            .root = 0,
                        }
                    };
                }
                else
                {
                    retVal = LEAF_TOKEN;
                    continue;
                }

                break;

            case CALL_LOOP:

                if (STRING != nextToken.type)
                {
                    retVal = PARSER_BAD_FORMAT; // key must be string
                    continue;
                }
                currentNode->key = nextToken.value.str;

                ASSERT_TOKEN(&nextToken);
                if (COLON != nextToken.type)
                {
                    retVal = PARSER_BAD_FORMAT; // expecting colon after key
                    continue;
                }

                previousNode = currentNode;

                ASSERT_AVAILABLE_SOFT_STACK;
                STACK[stackTop++] = (recursionStack_t){
                    .type = RESUME_LOOP,
                    .data =
                    {
                        .currentNode = currentNode
                    }
                };

                ASSERT_AVAILABLE_SOFT_STACK;
                STACK[stackTop++] = (recursionStack_t){
                    .type = CALL,
                    .data =
                    {
                        .root = 0,
                    }
                };

                break;

            case RESUME_LOOP:
                if (retVal == LEAF_TOKEN)
                {
                    memcpy(&currentNode->value, &nextToken.value, sizeof (currentNode->value));
                }
                else if (retVal != PARSER_OK)
                {
                    retVal = PARSER_BAD_FORMAT;
                    continue;
                }

                ASSERT_TOKEN(&nextToken);
                if (RIGHT_BRACE == nextToken.type)
                {
                    retVal = PARSER_OK;
                    continue;
                }
                else if (COMMA != nextToken.type)
                {
                    retVal = PARSER_BAD_FORMAT; // expecting comma
                    continue;
                }

                ASSERT_NODE_ALLOC(pop.data.currentNode->h);
                previousNode = pop.data.currentNode;
                currentNode = pop.data.currentNode->h;

                ASSERT_TOKEN(&nextToken);

                ASSERT_AVAILABLE_SOFT_STACK;
                STACK[stackTop++] = (recursionStack_t){
                    .type = CALL_LOOP
                };

                break;

            default:
                break;
        }
    }

    return retVal;
}


void MEM_MODEL_init(void)
{
    theJsonTop = 0;
}

void LEXER_init(char *str)
{
    current_possition = str;
}

jsonDecoderStatus_t JSON_DECODER_fromString(char *str)
{
    MEM_MODEL_init();
    LEXER_init(str);

    if (PARSER_OK != PARSER_parseTokens())
    {
        return JSON_DECODER_BAD_FORMAT;
    }

    return JSON_DECODER_OK;
}