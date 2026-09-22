#include <cstdio>
#include <cstdlib>
class Node { public: int item; Node* left; Node* right; Node(int i): item(i), left(nullptr), right(nullptr) {} Node(int i, Node* l, Node* r): item(i), left(l), right(r) {} };
int itemCheck(Node* n) { if (n->left == nullptr) return n->item; return n->item + itemCheck(n->left) - itemCheck(n->right); }
Node* bottomUpTree(int i, int d) { if (d > 0) return new Node(i, bottomUpTree(2*i-1, d-1), bottomUpTree(2*i, d-1)); return new Node(i, nullptr, nullptr); }
void freeTree(Node* n) { if (!n) return; freeTree(n->left); freeTree(n->right); delete n; }
int main() {
    int minD=4, maxD=10, sD=maxD+1;
    std::printf("Stretching tree of depth %d\n", sD);
    Node* st = bottomUpTree(0, sD);
    int ch = itemCheck(st); freeTree(st);
    std::printf("Item check: %d\n", ch);
    for (int depth=minD; depth<=maxD; depth+=2) {
        int it = 1 << (maxD-depth+minD);
        int sum = 0;
        for (int i=1; i<=it; i++) { Node* t1=bottomUpTree(i, depth); Node* t2=bottomUpTree(-i, depth); sum += itemCheck(t1) - itemCheck(t2); freeTree(t1); freeTree(t2); }
        std::printf("%d trees of depth %d item check: %d\n", it*2, depth, sum);
    }
    Node* ll = bottomUpTree(0, maxD);
    std::printf("Long lived tree of depth %d item check: %d\n", maxD, itemCheck(ll));
    freeTree(ll);
    return 0;
}
