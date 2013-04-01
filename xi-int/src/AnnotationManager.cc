#include "AnnotationManager.h"
#include "boost/spirit/include/qi.hpp"
#include "boost/spirit/include/phoenix_core.hpp"
#include "boost/spirit/include/phoenix_operator.hpp"
#include "boost/spirit/include/phoenix_fusion.hpp"
#include "boost/fusion/include/adapt_struct.hpp"
#include "boost/spirit/include/phoenix_stl.hpp"
#include "boost/variant/recursive_variant.hpp"

#include <iostream>
#include <fstream>
#include <string>

using std::cerr;

BOOST_FUSION_ADAPT_STRUCT(
	AnnotationExpValue,
	(AnnotationExpValue::ExpValueKind, Kind)
	(long long, D)
)

BOOST_FUSION_ADAPT_STRUCT(
	AnnotationExp,
	(std::vector<AnnotationTerm>, Terms)
	(std::vector<bool>, Signs)
)

struct AnnotationRule {
	typedef enum {
		Single,
		Range
	} RuleKind;
	RuleKind Kind;
	std::string Name;
	AnnotationExp V1;
	AnnotationExp V2;
};

BOOST_FUSION_ADAPT_STRUCT(
	AnnotationRule,
	(AnnotationRule::RuleKind, Kind)
	(std::string, Name)
	(AnnotationExp, V1)
	(AnnotationExp, V2)
)

namespace qi = boost::spirit::qi;
namespace phoenix = boost::phoenix;
namespace ascii = boost::spirit::ascii;

namespace {

struct AnnotationGrammar : qi::grammar<std::string::iterator, AnnotationRule(), ascii::space_type> {
	AnnotationGrammar() : AnnotationGrammar::base_type(AnnotationR) {
		using qi::lexeme;
		using boost::spirit::lit;
		using boost::spirit::int_;
		using ascii::char_;
		using ascii::string;
		using phoenix::at_c;
		using phoenix::push_back;
		using namespace qi::labels;

		ValueR = lexeme[(lit("arg") >> int_[at_c<0>(_val) = AnnotationExpValue::ArgumentKind][at_c<1>(_val) = _1])
			| int_[at_c<0>(_val) = AnnotationExpValue::IntKind][at_c<1>(_val) = _1]];
		TermR %= ValueR >> *('*' >> ValueR);
		ExpR = (TermR[push_back(at_c<0>(_val), _1)][push_back(at_c<1>(_val), true)]
			    | (lit("-")[push_back(at_c<1>(_val), true)] >> TermR[push_back(at_c<0>(_val), _1)] )) 
			>> *((lit("+")[push_back(at_c<1>(_val), true)] | lit("-")[push_back(at_c<1>(_val), false)]) 
			>> TermR[push_back(at_c<0>(_val), _1)]);
		NameR = lexeme[(lit("func+")[_val = "func+"] | lit("global+")[_val = "global+"]) >> +((char_ - ' ' - '\t')[_val += _1])];
		AnnotationR = (NameR[at_c<0>(_val) = AnnotationRule::Single][at_c<1>(_val) = _1] >> ExpR[at_c<2>(_val) = _1])
			| (NameR[at_c<0>(_val) = AnnotationRule::Range][at_c<1>(_val) = _1] >> '[' 
				>> ExpR[at_c<2>(_val) = _1] >> ',' >> ExpR[at_c<3>(_val) = _1] >> ']');
	}

	qi::rule<std::string::iterator, AnnotationExpValue(), ascii::space_type> ValueR;
	qi::rule<std::string::iterator, AnnotationTerm(), ascii::space_type> TermR;
	qi::rule<std::string::iterator, AnnotationExp(), ascii::space_type> ExpR;
	qi::rule<std::string::iterator, std::string(), ascii::space_type> NameR;
	qi::rule<std::string::iterator, AnnotationRule(), ascii::space_type> AnnotationR;
};

bool getConstant(const AnnotationExp &E, long long &v) {
	if (E.Terms.size() != 1) return false;
	if (E.Terms[0].size() != 1) return false;
	if (E.Terms[0][0].Kind != AnnotationExpValue::IntKind) return false;
	v = E.Terms[0][0].D;
	if (!E.Signs[0]) v = -v;
	return true;
}

} // anonymous namespace

AnnotationManager::AnnotationManager(const char* FileName) {
	cerr << "AnnotationManager::AnnotationManager FileName="  << FileName << "\n";
	const int MAXLINELEN = 1000;
	std::ifstream fin(FileName);
	char buf[MAXLINELEN];
	while (fin.getline(buf, MAXLINELEN)) {
		using boost::spirit::ascii::space;
		std::string line = buf;
		std::string::iterator iter = line.begin();
		AnnotationGrammar G;
		AnnotationRule R;
		bool ret = phrase_parse(iter, line.end(), G, space, R);
		if (!ret || iter != line.end()) {
			cerr << "Unable to parse annotation line: "  << line << "\n";
			cerr << "Ignored!\n";
			continue;
		}
		if (R.Name.substr(0, 5) == "func+") {
			R.Name = R.Name.substr(5);
			if (R.Kind == AnnotationRule::Single) {
				R.V2 = R.V1;
				AnnotationExpValue tmp;
				tmp.Kind = AnnotationExpValue::IntKind;
				tmp.D = 1;
				AnnotationTerm tmp2;
				tmp2.clear();
				tmp2.push_back(tmp);
				R.V2.Terms.push_back(tmp2);
				R.V2.Signs.push_back(true);
			}
			Func[R.Name] = std::make_pair(R.V1, R.V2);
		}
		else {
			R.Name = R.Name.substr(7);
			bool bad = false;
			long long l = 0, r = 0;
			if (!getConstant(R.V1, l)) bad = true;
			if (R.Kind == AnnotationRule::Range) {
				if (!getConstant(R.V2, r)) bad = true;
			}
			else 
				r = l + 1;
			if (bad) {
				cerr << "Global variable annotation should contain only constant.\n";
				cerr << "Line: " << line <<"\n";
				cerr << "Ignored!\n";
				continue;
			}
			Global[R.Name] = std::make_pair(l, r);
		}
	}
}
