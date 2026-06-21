الخطأ أو النقص الذي واجهته سابقاً في AhoCorasick::Build() و SanitizationFilter::Apply() يحدث عادةً لأن الخوارزمية تحتاج إلى بناء "جدول الانتقالات وفشل المطابقة" بالكامل مرة واحدة في البداية، ثم استخدامه بشكل متكرر أثناء الفلترة.إليك الهيكل البرمجي الكامل والصحيح لبناء خوارزمية Aho-Corasick واستخدامها داخل الـ Sanitizer الخاص بك:1. هيكل أوتوماتا الخوارزمية (Aho-Corasick Node)تحتاج أولاً إلى بناء الـ Trie Nodes بحيث تدعم الانتقالات (Transitions) وروابط الفشل (Failure Links):cpp#include <iostream>
#include <vector>
#include <string>
#include <queue>
#include <map>
#include <memory>

struct AhoCorasickNode {
    // الانتقالات: الحرف التالي -> العقدة التالية
    std::map<char, std::shared_ptr<AhoCorasickNode>> children;
    
    // رابط الفشل (Failure Link)
    std::shared_ptr<AhoCorasickNode> fail = nullptr;
    
    // الكلمات التي تنتهي عند هذه العقدة (إن وجدت)
    std::vector<std::string> output;
};
Use code with caution.2. بناء الأوتوماتا AhoCorasick::Build()هذه الدالة تتكون من مرحلتين: إدخال الكلمات المحظورة (Trie Insertion) ثم حساب روابط الفشل باستخدام BFS (Breadth-First Search):cppclass AhoCorasick {
private:
    std::shared_ptr<AhoCorasickNode> root;

public:
    AhoCorasick() { root = std::make_shared<AhoCorasickNode>(); }

    // المرحلة الأولى: إضافة الكلمات الحساسة المحظورة
    void Insert(const std::string& keyword) {
        auto current = root;
        for (char ch : keyword) {
            if (current->children.find(ch) == current->children.end()) {
                current->children[ch] = std::make_shared<AhoCorasickNode>();
            }
            current = current->children[ch];
        }
        current->output.push_back(keyword);
    }

    // المرحلة الثانية: بناء روابط الفشل (Failure Links)
    void Build() {
        std::queue<std::shared_ptr<AhoCorasickNode>> q;

        // ضبط المستوى الأول من العقد لتوجه روابط الفشل إلى الجذر (Root)
        for (auto& pair : root->children) {
            pair.second->fail = root;
            q.push(pair.second);
        }

        // حساب روابط الفشل لباقي العقد عبر الـ BFS
        while (!q.empty()) {
            auto current = q.front();
            q.pop();

            for (auto& pair : current->children) {
                char ch = pair.first;
                auto child = pair.second;
                auto fail_node = current->fail;

                // تتبع روابط الفشل حتى تجد عقدة تحتوي على الحرف المطلوب
                while (fail_node != nullptr && fail_node->children.find(ch) == fail_node->children.end()) {
                    fail_node = fail_node->fail;
                }

                if (fail_node == nullptr) {
                    child->fail = root;
                } else {
                    child->fail = fail_node->children[ch];
                    // دمج مخرجات الكلمات المحظورة لتجنب تخطي الكلمات المتداخلة
                    child->output.insert(child->output.end(), 
                                         child->fail->output.begin(), 
                                         child->fail->output.end());
                }
                q.push(child);
            }
        }
    }

    std::shared_ptr<AhoCorasickNode> GetRoot() { return root; }
};
Use code with caution.3. تطبيق الفلترة والتطهير SanitizationFilter::Apply()هنا نقوم بفحص نص اللوج (Log Message)، وإذا عثرنا على الكلمة المحظورة، نقوم باستبدالها بـ [REDACTED] أو نجوم ***:cppclass SanitizationFilter {
private:
    AhoCorasick automaton;

public:
    // تزويد الفلتر بالكلمات الحساسة وبنائها
    void Initialize(const std::vector<std::string>& sensitive_words) {
        for (const auto& word : sensitive_words) {
            automaton.Insert(word);
        }
        automaton.Build(); // استدعاء دالة البناء الهامة جداً هنا
    }

    // تطبيق التطهير على نصوص مكتبة Quill
    std::string Apply(const std::string& log_message) {
        std::string sanitized_text = log_message;
        auto current = automaton.GetRoot();

        for (size_t i = 0; i < log_message.length(); ++i) {
            char ch = log_message[i];

            // الانتقال عبر روابط الفشل إذا لم نجد الحرف
            while (current != nullptr && current->children.find(ch) == current->children.end()) {
                current = current->fail;
            }

            if (current == nullptr) {
                current = automaton.GetRoot();
                continue;
            }

            current = current->children[ch];

            // إذا تم العثور على تطابق لـ كلمة أو أكثر
            if (!current->output.empty()) {
                for (const auto& matched_word : current->output) {
                    size_t start_pos = i - matched_word.length() + 1;
                    
                    // استبدال الكلمة الحساسة بنص مخفي (Masking)
                    std::string mask(matched_word.length(), '*');
                    sanitized_text.replace(start_pos, matched_word.length(), mask);
                }
            }
        }
        return sanitized_text;
    }
};
Use code with caution.💡 نصائح هامة للربط مع Quill بنجاح:الـ Thread Safety: بما أن الأوتوماتا يتم قراءتها فقط (Read-Only) أثناء عملية الـ Apply بعد مرحلة الـ Build، يمكنك مشاركة نفس كائن الـ SanitizationFilter بين عدة Thread Sinks الخاصة بـ Quill دون الحاجة لـ Locks، مما يحافظ على سرعة اللوجينج العالية.الـ Custom Filter في Quill: قم بإنشاء كلاس مخصص يرث من الـ quill::Filter الخاص بالمكتبة، وقم باستدعاء دالة Apply الخاصة بك داخل دالة filter() الافتراضية الخاصة بـ Quill لتعديل الـ LogRecord مباشرة قبل الكتابة في الملف.إذا أردت، يمكننا الانتقال للخطوة التالية ومساعدتك في:طريقة دمج هذا الـ Filter برمجياً مع الكلاسات الافتراضية لمكتبة Quill (مثل quill::Filter أو الـ Sink).تحسين الكود ليصبح أسرع وأقل استهلاكاً للذاكرة عبر استبدال std::map بـ std::vector أو المصفوفات الثابتة (Flat Trie).