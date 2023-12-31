#include <stdlib.h>
#include "zmalloc.h"
#include "adlist.h"

list *listCreate(void){
    list *list;

    if((list = zmalloc(sizeof(*list))) == NULL)
        return NULL;
    list->head = list->tail = NULL;
    list->len = 0;
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;
    return list;
}

void listEmpty(list *list){
    unsigned long len = list->len;
    listNode *next, *current = list->head;

    while(len--){
        next = current->next;
        if(list->free){
            list->free(current->value);
            zfree(current);
            current = next;
        }
    }
    list->head = list->tail = NULL;
    list->len = 0;
}

void listRelease(list *list){
    listEmpty(list);
    zfree(list);
}

list *listAddNodeHead(list *list, void *value){
    listNode *node;

    if((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    listLinkNodeHead(list, node);
    return list;
}

void listLinkNodeHead(list *list, listNode *node){
    if(list->len == 0){
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    }else{
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }
    list->len++;
}

list *listAddNodeTail(list *list, void *value){
    listNode *node;

    if((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    listLinkNodeTail(list, node);
    return list;
}

void listLinkNodeTail(list *list, listNode *node){
    if(list->len == 0){
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    }else{
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }
}

list *listInsertNode(list *list, listNode *old_node, void *value, int after){
    listNode *node;

    if((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    if(after){
        node->prev = old_node;
        node->next = old_node->next;
        if(list->tail == old_node){
            list->tail = node;
        }
    }else{
        node->next = old_node;
        node->prev = old_node->prev;
        if(list->head == old_node){
            list->head = node;
        }
    }
    if(node->prev != NULL){
        node->prev->next = node;
    }
    if(node->next != NULL){
        node->next->prev = node;
    }
    list->len++;
    return list;
}

void listDelNode(list *list, listNode *node){
    listUnlinkNode(list, node);
    if(list->free)
        list->free(node->value);
    free(node);
}

void listUnlinkNode(list *list, listNode *node){
    if(node->prev)
        node->prev->next = node->next;
    else
        list->head = node->next;
    if(node->next)
        node->next->prev = node->prev;
    else
        list->head = node->prev;
    
    node->next = NULL;
    node->prev = NULL;
    list->len--;
}

listIter *listGetIterator(list *list, int direction){
    listIter *iter;

    if((iter = zmalloc(sizeof(*iter))) == NULL)
        return NULL;
    if(direction == 0)
        iter->next = list->head;
    else
        iter->next = list->tail;
    iter->direction = direction;
    return iter;
}

void listReleaseIterator(listIter *iter){
    zfree(iter);
}

void listRewind(list *list, listIter *li){
    li->next = list->head;
    li->direction = 0;
}

void listRewindTail(list *list, listIter *li){
    li->next = list->tail;
    li->direction = 1;
}

listNode *listNext(listIter *iter){
    listNode *current = iter->next;

    if(current != NULL){
        if(iter->direction == 0)
            iter->next = current->next;
        else
            iter->next = current->prev;
    }
    return current;
}

list *listDup(list *orig){
    list *copy;
    listIter iter;
    listNode *node;

    if((copy = listCreate()) == NULL)
        return NULL;
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;
    listRewind(orig, &iter);
    while((node = listNext(&iter)) != NULL){
        void *value;

        if(copy->dup){
            value = copy->dup(node->value);
            if(value == NULL){
                listRelease(copy);
                return NULL;
            }
        }else{
            value = node->value;
        }

        if(listAddNodeTail(copy, value) == NULL){
            if(copy->free)
                copy->free(value);

            listRelease(copy);
            return NULL;
        }
    }
    return copy;
}

listNode *listSearchKey(list *list, void *key){
    listIter iter;
    listNode *node;

    listRewind(list, &iter);
    while((node = listNext(&iter)) != NULL){
        if(list->match){
            if(list->match(node->value, key)){
                return node;
            }
        }else{
            if(key == node->value){
                return node;
            }
        }
    }
    return NULL;
}

listNode *listIndex(list *list, long index){
    listNode *n;

    if(index < 0){
        index = (-index) - 1;
        n = list->tail;
        while(index-- && n)
            n = n->prev;
    }else{
        n = list->head;
        while(index-- && n)
            n = n->next;
    }
    return n;
}

void listRotateTailToHead(list *list){
    if(list->len <= 1)
        return;

    listNode *tail = list->tail;
    list->tail = tail->prev;
    list->tail->next = NULL;
    list->head->prev = tail;
    tail->prev = NULL;
    tail->next = list->head;
    list->head = tail;
}

void listRotateHeadToTail(list *list){
    if(list->len <= 1)
        return;

    listNode *head = list->head;
    list->head = head->next;
    list->head->prev = NULL;
    list->tail->next = head;
    head->next = NULL;
    head->prev = list->tail;
    list->tail = head;
}

void listJoin(list *l, list *o){
    if(o->len == 0)
        return;

    o->head->prev = l->tail;

    if(l->tail)
        l->tail->next = o->head;
    else
        l->head = o->head;

    l->tail = o->tail;
    l->len += o->len;

    o->head = o->tail = NULL;
    o->len = 0;
}

void listInitNode(listNode *node, void *value){
    node->prev = NULL;
    node->next = NULL;
    node->value = value;
}