#ifdef __cplusplus
	#define Suffix "++"
	#if __cplusplus == 1
		#undef __cplusplus
		#define __cplusplus 199711
	#endif
	#define C_Number __cplusplus
	#define C_Year (__cplusplus / 100)
	#define C_Month (__cplusplus % 100)
	#define C_Type "CPP"
#else
	#ifdef __STDC_VERSION__
		#define C_Number __STDC_VERSION__
		#define C_Year (__STDC_VERSION__ / 100)
		#define C_Month (__STDC_VERSION__ % 100)
		#define C_Type "STD"
	#else
		#define C_Number 0
		#define C_Month 0
		#ifdef __STRICT_ANSI__
			#define C_Year 1989
			#define C_Type "ANSI"
		#else
			#ifdef __STDC__
				#define C_Year 1990
				#define C_Type "ISO"
			#else
				#define C_Year 1972
				#define C_Type "K&R"
			#endif
		#endif
	#endif
#endif
